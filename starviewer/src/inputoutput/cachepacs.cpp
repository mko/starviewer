/***************************************************************************
 *   Copyright (C) 2005 by Grup de Gr�fics de Girona                       *
 *   http://iiia.udg.es/GGG/index.html?langu=uk                            *
 *                                                                         *
 *   Universitat de Girona                                                 *
 ***************************************************************************/ 
#include "cachepacs.h"
#include "series.h"
#include "studylist.h"
#include "serieslist.h"
#include "status.h"
#include "image.h"
#include <time.h>
#include "cachepool.h"
#include <QString>
#include "seriesmask.h"
#include "studymask.h"
#include "logging.h"

namespace udg {

CachePacs::CachePacs()
{
   m_DBConnect = DatabaseConnection::getDatabaseConnection();
}

/**  Construeix l'estat en que ha finaltizat l'operaci� sol�licitada
  *            @param  [in] Estat de sqlite
  *            @return retorna l'estat de l'operaci�
  */
Status CachePacs::constructState(int numState)
{
//A www.sqlite.org/c_interface.html hi ha al codificacio dels estats que retorna el sqlite
    Status state;
	QString logMessage, codeError;

    switch(numState)
    {//aqui tractem els errors que ens poden afectar de manera m�s directe, i els quals l'usuari pot intentar solucionbar                         
        case SQLITE_OK :        state.setStatus("Normal",true,0);
                                break;
        case SQLITE_ERROR :     state.setStatus("Database is corrupted or SQL error syntax ",false,2001);
                                break;
        case SQLITE_BUSY :      state.setStatus("Database is locked",false,2011);
                                break;
        case SQLITE_CORRUPT :   state.setStatus("Database corrupted",false,2011);
                                break;
        case SQLITE_CONSTRAINT: state.setStatus("The new register is duplicated",false,2019);
                                break;
        case 50 :               state.setStatus("Not connected to database",false,2050);
                                break;
      //aquests errors en principi no es poden donar, pq l'aplicaci� no altera cap element de l'estructura, si es produeix algun
      //Error d'aquests en principi ser� perqu� la bdd est� corrupte o problemes interns del SQLITE, fent Numerror-2000 de l'estat
      //a la p�gina de www.sqlite.org podrem saber de quin error es tracta.
        default :               state.setStatus("SQLITE internal error",false,2000+numState); 
                                break;
    }

	if ( numState != SQLITE_OK )
	{
		logMessage = "Error a la cache n�mero " + codeError.setNum( numState , 10 );
		ERROR_LOG( logMessage.toAscii().constData() );
	}

   return state;
}


/************************************************************************************************************************
  *                                       ZONA INSERTS                                                                  *
  ***********************************************************************************************************************/
  
/** Afegeix un nou estudi i pacient a la bd local, quant comencem a descarregar un nou estudi.
  *   La informaci� que insereix �s :
  *        Si el pacient no existeix - PatientId
  *                                  - PatientName
  *                                  - PatientBirthDate
  *                                  - PatientSex  
  *
  *       Si l'estudi no existeix    - PatientID
  *                                  - StudyUID
  *                                  - StudyDate
  *                                  - StudyTime
  *                                  - StudyID
  *                                  - AccessionNumber
  *                                  - StudyDescription
  *                                  - Status
  *  La resta d'informaci� no estar� disponible fins que les imatges estiguin descarregades, 
  *                    
  *         @param Study[in]  Informaci� de l'estudi 
  *         @return retorna l'estat de l'inserci�                                    
  */
Status CachePacs::insertStudy(Study *stu)
{
    
    std::string insertPatient,insertStudy,sql,patientName;
    int i;
    Status state;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    // Hi ha noms del pacients que depenent de la m�quina tenen el nom format per cognoms^Nom, en aquest cas substituim ^ per espai
    patientName = stu->getPatientName();
    
    while ( patientName.find('^') != std::string::npos )
    {
        patientName.replace(patientName.find('^'),1," ",1);
    }
    
    m_DBConnect->getLock(); //s'insereix el pacient 
    i=sqlite_exec_printf(m_DBConnect->getConnection(),"Insert into Patient (PatId,PatNam,PatBirDat,PatSex) values (%Q,%Q,%Q,%Q)",0,0,0
                                ,stu->getPatientId().c_str()
                                ,patientName.c_str()
                                ,stu->getPatientBirthDate().c_str()
                                ,stu->getPatientSex().c_str()
                                );
    m_DBConnect->releaseLock();

    state = constructState(i);
    
    //si l'estat de l'operaci� �s fals, per� l'error �s el 2019, significa que el pacient, ja existia a la bdd, per tant 
    //continuem inserint l'estudi, si es provoca qualsevol altre error parem
    if (!state.good() && state.code() != 2019) return state; 
    
    sql.insert(0,"Insert into Study "); //crem el el sql per inserir l'estudi ,al final fem un select per assignar a l'estudi l'id del PACS al que pertany
    sql.append("(PatId, StuInsUID, StuID, StuDat, StuTim, RefPhyNam, AccNum, StuDes, Modali, ");
    sql.append("OpeNam, Locati, AccDat, AccTim, AbsPath, Status, PacsID, PatAge) ");
    sql.append("Values (%Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %i, %i, %Q, %Q, ");
    sql.append("(select PacsID from PacsList where AETitle = %Q), %Q)");//busquem l'id del PACS
    
    m_DBConnect->getLock();
    i=sqlite_exec_printf(m_DBConnect->getConnection(),sql.c_str(),0,0,0
                                ,stu->getPatientId().c_str()
                                ,stu->getStudyUID().c_str()
                                ,stu->getStudyId().c_str()
                                ,stu->getStudyDate().c_str()
                                ,stu->getStudyTime().c_str()
                                ,""                        //Referring Physician Name
                                ,stu->getAccessionNumber().c_str()
                                ,stu->getStudyDescription().c_str()
                                ,stu->getStudyModality().c_str()   //Modality
                                ,""                        //Operator Name
                                ,""                        //Location
                                ,getDate()                 //Access Date
                                ,getTime()                 //Access Time
                                ,stu->getAbsPath().c_str()
                                ,"PENDING"                 //estat pendent perqu� la descarrega de l'estudi encara no est� completa               
                                ,stu->getPacsAETitle().c_str()
                                ,stu->getPatientAge().c_str()
                                );
    m_DBConnect->releaseLock();
                                
                                
    state = constructState(i);
                                
    return state;
    
}

/** Insereix una s�rie a la cach�
  *        @param series [in] Dades de la s�rie
  *        @return retorna l'estat de la inserci�
  */
Status CachePacs::insertSeries(Series *serie)
{
    int i;
    Status state;
    std::string sql;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    sql.insert(0,"Insert into Series ");
    sql.append(" ( SerInsUID , SerNum , StuInsUID , SerMod , ProNam , SerDes , SerPath , BodParExa , SerDat , SerTim ) ");
    sql.append(" values ( %Q , %Q , %Q , %Q , %Q , %Q , %Q , %Q , %Q , %Q ) ");
    
    m_DBConnect->getLock();
    i = sqlite_exec_printf(m_DBConnect->getConnection(),sql.c_str(),0,0,0
                                ,serie->getSeriesUID().c_str()
                                ,serie->getSeriesNumber().c_str()
                                ,serie->getStudyUID().c_str()
                                ,serie->getSeriesModality().c_str()
                                ,serie->getProtocolName().c_str()
                                ,serie->getSeriesDescription().c_str()
                                ,serie->getSeriesPath().c_str()
                                ,serie->getBodyPartExaminated().c_str()
                                ,serie->getSeriesDate().c_str()
                                ,serie->getSeriesTime().c_str());
    m_DBConnect->releaseLock();
    
    state = constructState(i);
    return state;
}

/** Insereix la informaci� d'una imatge a la cach�. I actualitza l'espai ocupat de la pool, com s'ha de fer un insert i un update aquests dos operacions
  * es fan dins el marc d'una transaccio, per mantenir coherent l'espai de la pool ocupat
  *        @param [in] dades de la imatge 
  *         @return retorna estat del m�tode
  */
Status CachePacs::insertImage(Image *image)
{
    //no guardem el path de la imatge perque la el podem saber amb Study.AbsPath/SeriesUID/SopInsUID

    int i;
    Status state;
    std::string sql;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    sql.insert(0,"Insert into Image (SopInsUID, StuInsUID, SerInsUID, ImgNum, ImgTim,ImgDat, ImgSiz, ImgNam) ");
    sql.append("values (%Q,%Q,%Q,%i,%Q,%Q,%i,%Q)");
    
    m_DBConnect->getLock();
    i = sqlite_exec_printf(m_DBConnect->getConnection(),"BEGIN TRANSACTION ",0,0,0);//comencem la transacci�

    state = constructState(i);
    
    if (!state.good())
    {
        i = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }
    i = sqlite_exec_printf(m_DBConnect->getConnection(),sql.c_str(),0,0,0
                                ,image->getSoPUID().c_str()
                                ,image->getStudyUID().c_str()
                                ,image->getSeriesUID().c_str()
                                ,image->getImageNumber()
                                ,"0" //Image time
                                ,"0" //Image Date
                                ,image->getImageSize()
                                ,image->getImageName().c_str());   //IMage size
                                
    state = constructState(i);
    if (!state.good())
    {
        i = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }
                                    
    sql.clear();  
    sql.insert(0,"Update Pool Set Space = Space + %i ");
    sql.append("where Param = 'USED'");
    
    i = sqlite_exec_printf(m_DBConnect->getConnection(),sql.c_str(),0,0,0
                                ,image->getImageSize());
    
    state = constructState(i);
    if (!state.good())
    {
        i = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }
    
    i = sqlite_exec_printf(m_DBConnect->getConnection(),"COMMIT TRANSACTION ",0,0,0);
    
    m_DBConnect->releaseLock();
                                
    state = constructState(i);
    
    return state;
}

/*********************************************************************************************************************************************
 *                                                       ZONA DE LES QUERIES                                                                 *
 *********************************************************************************************************************************************/

/** Cerca els estudis que compleixen la m�scara a la cach�
  *            @param    M�scara de la cerca
  *            @param    StudyList amb els resultats
  *            @return retorna estat del m�tode
  */
Status CachePacs::queryStudy(StudyMask studyMask,StudyList &ls)
{

    int col,rows,i=0,estat;
    Study stu;

    char **resposta=NULL,**error=NULL;
    Status state;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    m_DBConnect->getLock();
    estat = sqlite_get_table(m_DBConnect->getConnection(), buildSqlQueryStudy( & studyMask ).c_str() ,&resposta,&rows,&col,error); //connexio a la bdd,sentencia sql,resposta, numero de files,numero de cols.
    m_DBConnect->releaseLock();
    state = constructState(estat);
    
    if (!state.good()) return state;
    
    i = 1;//ignorem les cap�aleres
    while (i <= rows)
    {   
        stu.setPatientId(resposta[0 + i*col]);
        stu.setPatientName(resposta[1 + i*col]);
        stu.setPatientAge(resposta[2+ i*col]);
        stu.setStudyId(resposta[3+ i*col]);
        stu.setStudyDate(resposta[4+ i*col]);
        stu.setStudyTime(resposta[5+ i*col]);
        stu.setStudyDescription(resposta[6+ i*col]);
        stu.setStudyUID(resposta[7+ i*col]);
        stu.setPacsAETitle(resposta[8 + i*col]);
        stu.setAbsPath(resposta[9 + i*col]);
        stu.setStudyModality(resposta[10 + i*col]);
        ls.insert(stu);
        i++;
    }
    
    return state;
    
}

/** Selecciona els estudis vells que no han estat visualitzats des de una data inferior a la que es passa per parametre
  *            @param  Data a partir de la qual es seleccionaran els estudis vells
  *            @param  StudyList amb els resultats dels estudis, que l'ultima vegada visualitzats es una data inferior a la passa per par�metre
  *            @return retorna estat del m�tode
  */
Status CachePacs::queryOldStudies(std::string OldStudiesDate , StudyList &ls)
{
    int col,rows,i=0,estat;
    Study stu;
    std::string sql;
    
    sql.insert(0,"select PatId, StuID, StuDat, StuTim, StuDes, StuInsUID, AbsPath, Modali ");
    sql.append(" from Study");
    sql.append(" where AccDat < ");
    sql.append( OldStudiesDate );
    sql.append(" and Status = 'RETRIEVED' ");
    sql.append(" order by StuDat,StuTim ");

    char **resposta=NULL,**error=NULL;
    Status state;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    m_DBConnect->getLock();
    estat = sqlite_get_table(m_DBConnect->getConnection(),sql.c_str(),&resposta,&rows,&col,error); //connexio a la bdd,sentencia sql,resposta, numero de files,numero de cols.
    m_DBConnect->releaseLock();
    state = constructState(estat);
    
    if (!state.good()) return state;
    
    i = 1;//ignorem les cap�aleres
    while (i <= rows)
    {   
        stu.setPatientId(resposta[0 + i*col]);
        stu.setStudyId(resposta[1 + i*col]);
        stu.setStudyDate(resposta[2 + i*col]);
        stu.setStudyTime(resposta[3 + i*col]);
        stu.setStudyDescription(resposta[4 + i*col]);
        stu.setStudyUID(resposta[5 + i*col]);
        stu.setAbsPath(resposta[6 + i*col]);
        stu.setStudyModality(resposta[7 + i*col]);
        ls.insert(stu);
        i++;
    }
    
    return state;
    
}

/** Cerca l'estudi que compleix amb la m�scara de cerca. Cerca ens els estudis que estan en estat Retrived o Retrieving
  *            @param    M�scara de  la cerca
  *            @param    StudyList amb els resultats
  *            @return retorna estat del m�tode
  */
Status CachePacs::queryStudy(std::string studyUID,Study &study)
{

    int col,rows,i=0,estat;

    char **resposta=NULL,**error=NULL;
    Status state;
    std::string sql;
    
    sql.insert(0,"select Study.PatId, PatNam, PatAge, StuID, StuDat, StuTim, StuDes, StuInsUID, AETitle, AbsPath, Modali ");
    sql.append(" from Patient,Study,PacsList ");
    sql.append(" where Study.PatID=Patient.PatId ");
    sql.append(" and Status in ('RETRIEVED','RETRIEVING') ");
    sql.append(" and PacsList.PacsID=Study.PacsID"); //busquem el nom del pacs
    sql.append(" and StuInsUID = '");
    sql.append(studyUID);
    sql.append("'");
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    m_DBConnect->getLock();
    estat = sqlite_get_table(m_DBConnect->getConnection(),sql.c_str(),&resposta,&rows,&col,error); //connexio a la bdd,sentencia sql,resposta, numero de files,numero de cols.
    m_DBConnect->releaseLock();
    state = constructState(estat);
    
    if (!state.good()) return state;
    
    i = 1;//ignorem les cap�aleres
    if (rows > 0) 
    {
        study.setPatientId(resposta[0 + i*col]);
        study.setPatientName(resposta[1 + i*col]);
        study.setPatientAge(resposta[2+ i*col]);
        study.setStudyId(resposta[3+ i*col]);
        study.setStudyDate(resposta[4+ i*col]);
        study.setStudyTime(resposta[5+ i*col]);
        study.setStudyDescription(resposta[6+ i*col]);
        study.setStudyUID(resposta[7+ i*col]);
        study.setPacsAETitle(resposta[8 + i*col]);
        study.setAbsPath(resposta[9 + i*col]);
        study.setStudyModality(resposta[10 + i*col]);
    }
    else
    { 
        estat = 99; //no trobat
        state = constructState(estat);
    }
    
    return state;
    
}

/** Construeix la sent�ncia sql per fer la query de l'estudi en funci� dels parametres de cerca
  *         @param mascara de cerca
  *         @return retorna estat del m�tode
  */
std::string CachePacs::buildSqlQueryStudy(StudyMask* studyMask)
{
    std::string sql,patientName,patID,stuDatMin,stuDatMax,stuID,accNum,stuInsUID,stuMod,studyDate;
    
    sql.insert(0,"select Study.PatId, PatNam, PatAge, StuID, StuDat, StuTim, StuDes, StuInsUID, AETitle, AbsPath, Modali ");
    sql.append(" from Patient,Study,PacsList ");
    sql.append(" where Study.PatID=Patient.PatId ");
    sql.append(" and Status = 'RETRIEVED' ");
    sql.append(" and PacsList.PacsID=Study.PacsID"); //busquem el nom del pacs
    
    //llegim la informaci� de la m�scara
    patientName = replaceAsterisk( studyMask->getPatientName() );
    patID = studyMask->getPatientId();
    studyDate = studyMask->getStudyDate();
    stuID = studyMask->getStudyId();
    accNum = studyMask->getAccessionNumber();
    stuMod = studyMask->getStudyModality();
    stuInsUID = studyMask->getStudyUID();
    
    //cognoms del pacient
    if (patientName.length() > 0)
    {
        sql.append(" and PatNam like '");
        sql.append(patientName);
        sql.append("' ");
    }          
    
    //Id del pacient
    if (patID != "*" && patID.length() > 0)
    {
        sql.append(" and Study.PatID = '");
        sql.append(patID);
        sql.append("' ");
    }
    
    //data
    if ( studyDate.length() == 8 )
    {
        sql.append( " and StuDat = '" );
        sql.append( studyDate );
        sql.append( "' " );        
    }
    else if ( studyDate.length() == 9) 
    {
        if ( studyDate.at( 0 ) == '-' )
        {
            sql.append( " and StuDat <= '" );
            sql.append( studyDate.substr( 1 , 8 ) );
            sql.append( "' " );
        }
        else if ( studyDate.at( 8 ) == '-' )
        {
            sql.append( " and StuDat >= '" );
            sql.append( studyDate.substr( 0 , 8 ) );
            sql.append( "' " ); 
        }
    }
    else if ( studyDate.length() == 17 )
    {
        sql.append( " and StuDat between '" );
        sql.append( studyDate.substr( 0 , 8 ) );
        sql.append( "' and '" );
        sql.append( studyDate.substr( 9, 8 ) );
        sql.append( "'" );
    }
    
    //id estudi
    
    if (stuID != "*" && stuID.length() > 0)
    {
        sql.append(" and StuID = '");
        sql.append(stuID);
        sql.append("' ");
    }
    
    //Accession Number
    if (accNum != "*" && accNum.length() > 0)
    {
        sql.append(" and AccNum = '");
    
        sql.append(accNum);
        sql.append("' ");
    }
    
    if (stuInsUID != "*" && stuInsUID.length() > 0)
    {
        sql.append(" and StuInsUID = '");
        sql.append(stuInsUID);
        sql.append("' ");        
    }
    
    if (stuMod != "*" && stuMod.length() > 0)
    {
        sql.append(" and Modali in ");
        sql.append(stuMod);
    }
    
    return sql;
}

/** Cerca les s�ries demanades a la m�scara. Important! Aquesta acci� nom�s t� en compte l'StudyUID de la m�scara per fer la cerca, els altres camps de la m�scara
  * els ignorar�!
  *         @param  mascar� de la serie amb l' sstudiUID a buscar
  *         @param  retorna la llista amb la s�ries trobades
  *         @return retorna estat del m�tode
  */
Status CachePacs::querySeries(SeriesMask seriesMask,SeriesList &ls)
{

    DcmDataset* mask = NULL;
    int col,rows,i = 0,estat;
     Series series;
    char **resposta = NULL,**error = NULL;
    Status state;
        
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }    
        
    mask = seriesMask.getSeriesMask();
                     
    m_DBConnect->getLock();
    estat = sqlite_get_table(m_DBConnect->getConnection() , buildSqlQuerySeries( &seriesMask ).c_str() ,&resposta,&rows,&col,error); //connexio a la bdd,sentencia sql,resposta, numero de files,numero de cols.
    m_DBConnect->releaseLock();
    
    state = constructState(estat);
    if (!state.good()) return state;
    
    i = 1;//ignorem les cap�aleres
    while (i <= rows)
    {   
        series.setSeriesUID(resposta[0 + i*col]);
        series.setSeriesNumber(resposta[1 + i*col]);
        series.setStudyUID(resposta[2 + i*col]);
        series.setSeriesModality(resposta[3 + i*col]);
        series.setSeriesDescription(resposta[4 + i*col]);
        series.setProtocolName(resposta[5 + i*col]);
        series.setSeriesPath(resposta[6 + i*col]);
        series.setBodyPartExaminated(resposta[7 + i*col]);
        series.setSeriesDate( resposta[8 + i*col] );
        series.setSeriesTime( resposta[9 + i*col] );
        ls.insert(series);
        i++;
    }
    return state;
}

/** Construeix la sent�ncia per buscar les s�ries d'un estudi
  *            @param mask [in] m�scara de cerca
  *            @return sent�ncia sql
  */
std::string CachePacs::buildSqlQuerySeries( SeriesMask *seriesMask )
{
    std::string sql;
    
    sql.insert(0,"select SerInsUID , SerNum , StuInsUID , SerMod , SerDes , ProNam, SerPath , BodParExa ");
    sql.append(", SerDat , SerTim ");
    sql.append(" from series where StuInsUID = '");
    sql.append( seriesMask->getStudyUID() );
    sql.append("'");
    
    cout<<sql<<endl;
    return sql;
}

/** Cerca les imatges demanades a la m�scara. Important! Aquesta acci� nom�s t� en compte l'StudyUID i el SeriesUID de la m�scara per fer la cerca, els altres 
  * caps de la m�scara els ignorar�!
  *         @param  mascara de les imatges a cercar
  *         @param llistat amb les imatges trobades
  *         @return retorna estat del m�tode
  */
Status CachePacs::queryImages(ImageMask imageMask,ImageList &ls)
{

    int col,rows,i = 0,estat;
    Image image;
    char **resposta = NULL,**error = NULL;
    Status state;
    std::string absPath;
        
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }    
        
    m_DBConnect->getLock();
    estat = sqlite_get_table(m_DBConnect->getConnection(), buildSqlQueryImages( &imageMask ).c_str() ,&resposta,&rows,&col,error); //connexio a la bdd,sentencia sql,resposta, numero de files,numero de cols.
    m_DBConnect->releaseLock();
    
    state = constructState(estat);
    if (!state.good()) return state;
    
    i = 1;//ignorem les cap�aleres
    while (i <= rows)
    {   
        image.setImageNumber(atoi(resposta[0 + i*col]));
        
        //creem el path absolut
        absPath.erase();
        absPath.insert(0,resposta[1 + i*col]);
        absPath.append(resposta[3 + i*col]); //incloem el directori de la serie
        absPath.append("/");
        absPath.append(resposta[5 + i*col]); //incloem el nom de la imatge
        image.setImagePath(absPath.c_str());
        
        image.setStudyUID(resposta[2 + i*col]);
        image.setSeriesUID(resposta[3 + i*col]);
        image.setSoPUID(resposta[4 + i*col]);        
        
        image.setImageName(resposta[5 + i *col]);
        ls.insert(image);
        i++;
    }
    
    return state;
}

/** Construeix la sent�ncia per buscar les Imatges d'una s�rie
  *            @param mask [in] m�scara de cerca
  *            @return sent�ncia sql
  */
std::string CachePacs::buildSqlQueryImages( ImageMask *imageMask )
{
    std::string sql,imgNum;
    
    sql.insert(0,"select ImgNum, AbsPath, Image.StuInsUID, SerInsUID, SopInsUID, ImgNam from image,study where Image.StuInsUID = '");
    sql.append( imageMask->getStudyUID() );
    sql.append("' and SerInsUID = '");
    sql.append( imageMask->getSeriesUID() );
    sql.append("' and Study.StuInsUID = Image.StuInsUID ");
    
    imgNum = imageMask->getImageNumber();
    
    if (imgNum.length() > 0)
    {
        sql.append(" and ImgNum = ");
        sql.append(imgNum);
        sql.append(" ");
    }
    
    sql.append(" order by ImgNum");
    
    return sql;
}

/** compta les imatges d'una s�rie 
  *            @param series [in] mascar� de la serie a comptar les images. Las m�scara ha de contenir el UID de l'estudi i de la s�rie
  *            @param imageNumber [out] conte el nombre d'imatges
  *            @return retorna estat del m�tode  
  */
Status CachePacs::countImageNumber(ImageMask imageMask,int &imageNumber)
{
    int col,rows,i = 0,estat;
    Series series;
    char **resposta = NULL,**error = NULL;
    Status state;
    std::string sql;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    m_DBConnect->getLock();
    estat = sqlite_get_table(m_DBConnect->getConnection(), buildSqlCountImageNumber( &imageMask ).c_str() ,&resposta,&rows,&col,error);;
    m_DBConnect->releaseLock();
    
    state = constructState(estat);
    
    if (!state.good()) return state;
    
    i = 1;//ignorem les cap�aleres
   
    imageNumber = atoi(resposta[i]);
   
   return state;
}

/** Construiex la sent�ncia sql per comptar el nombre d'imatges de la s�rie d'un estudi
  *            @param mask [in]
  */
std::string CachePacs::buildSqlCountImageNumber( ImageMask *imageMask )
{
    std::string sql;
    
    sql.insert(0,"select count(*) from image where StuInsUID = '");
    sql.append( imageMask->getStudyUID() );
    sql.append("' and SerInsUID = '");
    sql.append( imageMask->getSeriesUID() );
    sql.append("'");

    return sql;
}

/****************************************************************************************************************************************************
 *                                                    ZONA DELETE                                                                                   *
 ****************************************************************************************************************************************************
 */


/** Esborra un estudi de la cache, l'esborra la taula estudi,series, i image, i si el pacient d'aquell estudi, no t� cap altre estudi a la cache local
  * tambe esborrem el pacient
  *            @param std::string [in] UID de l'estudi
  *            @return estat de l'operaci�
  */
Status CachePacs::delStudy(std::string studyUID)
{
    Status state;
    int estat;
    char **resposta = NULL,**error = NULL;
    int col,rows,studySize,i;
    std::string sql,absPathStudy;
    CachePool cachePool;

    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }

    /* La part d'esborrar un estudi com que s'ha d'accedir a diverses taules, ho farem en un transaccio per si falla alguna 
      sentencia sql fer un rollback, i aix� deixa la taula en estat estable, no deixem anar el candau fins al final */ 
    m_DBConnect->getLock();
    estat = sqlite_exec_printf(m_DBConnect->getConnection(),"BEGIN TRANSACTION ",0,0,0);//comencem la transacci�

    state=constructState(estat);
    if (!state.good())
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }

    //sql per saber el directori on es guarda l'estudi
    sql.clear();
    sql.insert(0,"select AbsPath from study where StuInsUID = '");
    sql.append(studyUID);
    sql.append("'");
      

    estat = sqlite_get_table(m_DBConnect->getConnection(),sql.c_str(),&resposta,&rows,&col,error); //connexio a la bdd,sentencia sql,resposta, numero de files,numero de cols.
     
    state = constructState(estat);
    if (!state.good())
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }       
    else if ( rows == 0 )
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return constructState( 99 );//error 99 registre no trobat           
    }
    else
    {
        absPathStudy = resposta[1];
    }
    
    //sql per saber quants estudis te el pacient
    sql.clear();
    sql.insert(0,"select count(*) from study where PatID in (select PatID from study where StuInsUID = '");
    sql.append(studyUID);
    sql.append("')");
      

    estat = sqlite_get_table(m_DBConnect->getConnection(),sql.c_str(),&resposta,&rows,&col,error); //connexio a la bdd,sentencia sql,resposta, numero de files,numero de cols.
     
    state = constructState(estat);
    if (!state.good())
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }   
    else if ( rows == 0 )
    {    
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return constructState ( 99 );//error 99 registre no trobat   
    }
    
    //ignorem el resposta [0], perque hi ha la cap�alera
    if (atoi(resposta[1]) == 1)
    {//si aquell pacient nomes te un estudi l'esborrem de la taula Patient
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"delete from Patient where PatID in (select PatID from study where StuInsUID = %Q)",0,0,0
                                ,studyUID.c_str());
                                
        state = constructState(estat);
        if (!state.good())
        {
            estat=sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
            m_DBConnect->releaseLock();
            return state;
        }    
    }
    
    //esborrem de la taula estudi    
    estat = sqlite_exec_printf(m_DBConnect->getConnection(),"delete from study where StuInsUID= %Q",0,0,0,studyUID.c_str());
    
    state = constructState(estat);
    if (!state.good())
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }

    //esborrem de la taula series
    estat = sqlite_exec_printf(m_DBConnect->getConnection(),"delete from series where StuInsUID= %Q",0,0,0,studyUID.c_str());
    state = constructState(estat);
    if (!state.good())
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }
    
//     //calculem el que ocupava l'estudi per actualitzar l'espai actualitzat
    sql.clear();
    sql.insert(0,"select sum(ImgSiz) from image where StuInsUID= '");
    sql.append(studyUID);
    sql.append("'");
    estat = sqlite_get_table(m_DBConnect->getConnection(),sql.c_str(),&resposta,&rows,&col,error);
    
    state = constructState(estat);
    if (!state.good())
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }
    i = 1;//ignorem les cap�aleres
    studySize = atoi(resposta[i]);

      
    //esborrem de la taula image
    estat = sqlite_exec_printf(m_DBConnect->getConnection(),"delete from image where StuInsUID= %Q",0,0,0,studyUID.c_str());
    state = constructState(estat);
    if (!state.good())
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }

    sql.clear();    
    sql.insert(0,"Update Pool Set Space = Space - %i ");
    sql.append("where Param = 'USED'");
    
    estat = sqlite_exec_printf(m_DBConnect->getConnection(),sql.c_str(),0,0,0
                                ,studySize);
                                
    state = constructState(estat);
    if (!state.good())
    {
        estat = sqlite_exec_printf(m_DBConnect->getConnection(),"ROLLBACK TRANSACTION ",0,0,0);
        m_DBConnect->releaseLock();
        return state;
    }
        
    estat = sqlite_exec_printf(m_DBConnect->getConnection(),"COMMIT TRANSACTION",0,0,0); //fem commit
    state = constructState(estat);
    if (!state.good())
    {
        return state;
    }
    
    m_DBConnect->releaseLock();        
    
    //una vegada hem esborrat les dades de la bd, podem esborrar les imatges, aix� s'ha de fer al final, perqu� si hi ha un error i esborrem les
    //imatges al principi, no les podrem recuperar i la informaci� a la base de dades hi continuar� estant
    cachePool.removeStudy(absPathStudy);
    
    return state;
   
}


/** Aquesta acci� es per mantenir la coherencia de la base de dades, si ens trobem estudis al iniciar l'aplicaci� que tenen l'estat pendent o descarregant
  * vol dir que l'aplicaci� en l'anterior execussi� ha finalitzat an�malament, per tant aquest estudis en estat pendents, les seves s�rie i 
  * imatges han de ser borrades perqu� es puguin tornar a descarregar. Aquesta acci� �s simplement per seguretat!
  *            @return estat de l'operaci�
  */
Status CachePacs::delNotRetrievedStudies()
{
    Status state;
    int estat;
    char **resposta = NULL,**error = NULL;
    int col,rows,i;
    std::string sql,studyUID;

    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    //cerquem els estudis pendents de finalitzar la descarrega
    sql.insert(0,"select StuInsUID from Study where Status in ('PENDING','RETRIEVING')");
   
    m_DBConnect->getLock();
    estat = sqlite_get_table(m_DBConnect->getConnection(),sql.c_str(),&resposta,&rows,&col,error); //connexio a la bdd,sentencia sql,resposta, numero de files,numero de cols.
    m_DBConnect->releaseLock();
    
    state = constructState(estat);
    if (!state.good()) return state;
   
    //ignorem el resposta [0], perque hi ha la cap�alera
    i = 1;
    
    while (i <= rows)
    {   
        studyUID.erase();
        studyUID.insert(0,resposta[i]);
        state = delStudy(studyUID);
        if (!state.good())
        {
            break;
        }
        i++;
    }
    
    return state;
   
}


/************************************************************************************************************************************************
 *                                                        ZONA UPDATES                                                                          *
 ************************************************************************************************************************************************
 */

/** Updata un estudi a Retrieved
  *        @param Uid de l'estudi a actualitzar
  *        @return retorna estat del m�tode
  */
Status CachePacs::setStudyRetrieved(std::string studyUID)
{
    int i;
    Status state;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    m_DBConnect->getLock();
    i = sqlite_exec_printf(m_DBConnect->getConnection(),"update study set Status = %Q where StuInsUID= %Q",0,0,0,"RETRIEVED",studyUID.c_str());
    m_DBConnect->releaseLock();
                                
    state=constructState(i);

    return state;
                                
}

/** Updata un estudi PENDING a RETRIEVING, per indicar que l'estudi s'esta descarregant
  *        @param Uid de l'estudi a actualitzar
  *        @return retorna estat del m�tode
  */
Status CachePacs::setStudyRetrieving(std::string studyUID)
{
    int i;
    Status state;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    m_DBConnect->getLock();
    i = sqlite_exec_printf(m_DBConnect->getConnection(),"update study set Status = %Q where StuInsUID= %Q",0,0,0,"RETRIEVING",studyUID.c_str());
    m_DBConnect->releaseLock();
                                
    state=constructState(i);

    return state;
                                
}

/** Updata la modalitat d'un estudi
  *        @param Uid de l'estudi a actualitzar
  *        @param Modalitat de l'estudi
  *        @return retorna estat del m�tode
  */
Status CachePacs::setStudyModality(std::string studyUID,std::string modality)
{
    int i;
    Status state;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    m_DBConnect->getLock();
    i = sqlite_exec_printf(m_DBConnect->getConnection(),"update study set Modali = %Q where StuInsUID= %Q",0,0,0,modality.c_str(),
              studyUID.c_str());
    m_DBConnect->releaseLock();
                                
    state=constructState(i);

    return state;
                                
}

/** actualitza l'�ltima vegada que un estudi ha estat visualitzat, d'aquesta manera quant haguem d'esborrar estudis
  * autom�ticament per falta d'espai, esborarrem els que fa m�s temps que no s'han visualitzat
  *        @param UID de l'estudi
  *        @param hora de visualitzaci� de l'estudi format 'hhmm'
  *        @param data visualitzaci� de l'estudi format 'yyyymmdd'
  *        @return estat el m�tode
  */
Status CachePacs::updateStudyAccTime(std::string studyUID)
{
    int i;
    Status state;
    std::string sql;
    
    //sqlite no permet en un update entra valors mes gran que un int, a trav�s de la interf�cie c++ com guardem la mida en bytes fem
    //un string i hi afegim 6 zeros per passar Mb a bytes

    sql.insert(0,"Update Study Set AccDat = %i, ");//convertim l'espai en bytes
    sql.append("AccTim = %i ");
    sql.append("where StuInsUID = %Q");

    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }

    m_DBConnect->getLock();
    i = sqlite_exec_printf(m_DBConnect->getConnection(),sql.c_str(),0,0,0
                                ,getDate()
                                ,getTime()
                                ,studyUID.c_str());
    m_DBConnect->releaseLock();
                                
    state=constructState(i);

    return state;
}


/***************************************************************************************************************************************
 *                                                MANTENIMENT DE LA CACHE                                                              *
 ***************************************************************************************************************************************
 */

/** Compacta la base de dades de la cache, per estalviar espai
  *        @return estat del m�tode  
  */
Status CachePacs::compactCachePacs()
{
    int i;
    Status state;
    std::string sql;
    
    if (!m_DBConnect->connected())
    {//el 50 es l'error de no connectat a la base de dades
        return constructState(50);
    }
    
    sql.insert(0,"vacuum");//amb l'acci� vacuum es compacta la base de dades
    
    m_DBConnect->getLock();
    i = sqlite_exec_printf(m_DBConnect->getConnection(),sql.c_str(),0,0,0);
    m_DBConnect->releaseLock();
                                
    state = constructState(i);

    return state;
}

/** retorna l'hora del sistema
  *     @return retorna l'hora del sistema en format HHMM
  */
int CachePacs::getTime()
{
  time_t hora;
  char cad[5];
  struct tm *tmPtr;

  hora = time(NULL);
  tmPtr = localtime(&hora);
  strftime( cad, 5, "%H%M", tmPtr );
  
  return atoi(cad);
}

/** retorna la data del sistema
  *    @return retorna la data del sistema en format yyyymmdd
  */
int CachePacs::getDate()
{
  time_t hora;
  char cad[9];
  struct tm *tmPtr;

  hora = time(NULL);
  tmPtr = localtime(&hora);
  strftime( cad, 9, "%Y%m%d", tmPtr );
  
  return atoi(cad);
}

/** Converteix l'asterisc, que conte el tag origen per %, per transformar-lo a la sintaxis de sql
  *     @param string original
  *     @retrun retorna l'string original, havent canviat els '*' per '%'
  */
std::string CachePacs::replaceAsterisk(std::string original)
{
    std::string ret;
    
    ret = original;
    
    //string::npos es retorna quan no s'ha trobat el "*"
    while ( ret.find("*") != std::string::npos )
    {
        ret.replace( ret.find( "*" ) , 1 , "%" , 1 );
    }
    
    return ret;
}

/** Destructor de la classe
  */
CachePacs::~CachePacs()
{

}

};
