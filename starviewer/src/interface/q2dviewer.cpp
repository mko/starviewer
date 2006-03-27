/***************************************************************************
 *   Copyright (C) 2005 by Grup de Gr�fics de Girona                       *
 *   http://iiia.udg.es/GGG/index.html?langu=uk                            *
 *                                                                         *
 *   Universitat de Girona                                                 *
 ***************************************************************************/
#include "q2dviewer.h"
#include "volume.h"

// include's qt
#include <QResizeEvent>
#include <QSize>
#include <QMenu>
#include <QAction>

// Tools
#include "distancetool.h"

// include's vtk
#include <QVTKWidget.h>
#include <vtkEventQtSlotConnect.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkImageViewer2.h>
#include <vtkImageCheckerboard.h>
#include <vtkImageBlend.h> // per composar les imatges
#include <vtkImageMapToWindowLevelColors.h>
#include <vtkLookupTable.h>
#include <vtkImageRectilinearWipe.h>
#include <vtkCellPicker.h>
#include <vtkInteractorStyleUser.h>
#include <vtkCornerAnnotation.h>
#include <vtkTextProperty.h>
#include <vtkAssemblyPath.h>
#include <vtkAxisActor2D.h>
#include <vtkProperty2D.h>
#include <vtkTextActor.h>
#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <vtkPNMWriter.h>
#include <vtkJPEGWriter.h>
#include <vtkTIFFWriter.h>
// interacci�
#include <vtkCell.h>
#include <vtkPointData.h>


namespace udg {


Q2DViewer::Q2DViewer( QWidget *parent , unsigned int annotations )
 : QViewer( parent )
{
    m_enabledAnnotations = annotations;
    m_lastView = None; 
    m_viewer = vtkImageViewer2::New();
    
    m_currentSlice = 0;
    m_overlay = CheckerBoard; // per defecte
    m_overlayVolume = 0;

// par�metres espec�fics
    
    // CheckerBoard
    // el nombre de divisions per defecte, ser� de 2, per simplificar
    m_divisions[0] = m_divisions[1] = m_divisions[2] = 2;
    
    // preparem el picker
    m_cellPicker = vtkCellPicker::New();
    m_cellPicker->SetTolerance( 0.005 );
    
    // ANOTACIONS
    createAnnotations();
    
    m_currentTool = NoTool;
    
    m_leftButtonAction = Q2DViewer::CursorAction;
    m_middleButtonAction = Q2DViewer::SliceMotionAction;
    m_rightButtonAction = Q2DViewer::WindowLevelAction;
    
    createActions();    
    createTools();
    
    updateCursor( -1, -1, -1, -1 );
    m_windowToImageFilter->SetInput( this->getRenderer()->GetRenderWindow() );
}

Q2DViewer::~Q2DViewer()
{
}

vtkRenderer *Q2DViewer::getRenderer()
{
    if( m_viewer )
        return m_viewer->GetRenderer();
    else
        return NULL;
}

vtkRenderWindowInteractor *Q2DViewer::getInteractor()
{
    return m_vtkWidget->GetRenderWindow()->GetInteractor();
}

void Q2DViewer::createActions()
{
    m_resetAction = new QAction( this );
    m_resetAction->setText(tr("&Reset"));
    m_resetAction->setShortcut( tr("Ctrl+R") );
    m_resetAction->setStatusTip(tr("Reset initial parameters"));
    connect( m_resetAction, SIGNAL( activated() ), this, SLOT( reset() ) );
}

void Q2DViewer::createTools()
{
    m_distanceTool = new DistanceTool( this );
}

void Q2DViewer::createAnnotations()
{
    // anotacions textuals
    m_textAnnotation = vtkCornerAnnotation::New();
    initInformationText();

    // informaci� de refer�ncia de la orientaci� del pacient
    for( int i = 0; i<4; i++ )
    {
        m_patientOrientationTextActor[i] = vtkTextActor::New();
        m_patientOrientationTextActor[i]->ScaledTextOff();
        m_patientOrientationTextActor[i]->GetTextProperty()->SetFontSize( 12 );
        m_patientOrientationTextActor[i]->GetTextProperty()->BoldOn();

        m_patientOrientationTextActor[i]->GetPositionCoordinate()->SetCoordinateSystemToNormalizedViewport();
        m_patientOrientationTextActor[i]->GetPosition2Coordinate()->SetCoordinateSystemToNormalizedViewport();

        this->getRenderer()->AddActor2D( m_patientOrientationTextActor[i] );
    }
    // ara posem la informaci� concreta de cadascuna de les refer�ncia d'orientaci�. 0-4 en sentit anti-horari, comen�ant per 0 = esquerra de la pantalla
    // les orientacions que donem ara s�n per posar algo, aqu� \TODO cal posar les refer�ncies correctes a partir de la informaci� dels tags DICOM
    m_patientOrientationTextActor[0]->SetInput( tr("LEFT").toAscii() );
    m_patientOrientationTextActor[0]->GetTextProperty()->SetJustificationToLeft();
    m_patientOrientationTextActor[0]->SetPosition( 0.01 , 0.5 );

    m_patientOrientationTextActor[1]->SetInput( tr("ANTERIOR").toAscii() );
    m_patientOrientationTextActor[1]->GetTextProperty()->SetJustificationToCentered();
    m_patientOrientationTextActor[1]->SetPosition( 0.5 , 0.01 );

    m_patientOrientationTextActor[2]->SetInput( tr("RIGHT").toAscii() );
    m_patientOrientationTextActor[2]->GetTextProperty()->SetJustificationToRight();
    m_patientOrientationTextActor[2]->SetPosition( 0.99 , 0.5 );

    m_patientOrientationTextActor[3]->SetInput( tr("SUPERIOR").toAscii() );
    m_patientOrientationTextActor[3]->GetTextProperty()->SetJustificationToCentered();
    m_patientOrientationTextActor[3]->SetPosition( 0.5 , 0.95 );
    
    // Marcadors
    m_sideOrientationMarker = vtkAxisActor2D::New();
    m_bottomOrientationMarker = vtkAxisActor2D::New();

    m_sideOrientationMarker->AxisVisibilityOn(); 
    m_sideOrientationMarker->TickVisibilityOn();
    m_sideOrientationMarker->LabelVisibilityOff();
    m_sideOrientationMarker->TitleVisibilityOff();
    m_sideOrientationMarker->SetTickLength( 50 );
    m_sideOrientationMarker->GetProperty()->SetColor( 0 , 1 , 0 );

//     this->getRenderer()->AddActor2D( m_sideOrientationMarker );

    m_bottomOrientationMarker->AxisVisibilityOn();
    m_bottomOrientationMarker->TickVisibilityOn();
    m_bottomOrientationMarker->LabelVisibilityOff();
    m_bottomOrientationMarker->TitleVisibilityOff();
    m_bottomOrientationMarker->SetTickLength( 50 );
    m_bottomOrientationMarker->GetProperty()->SetColor( 0 , 1 , 0 );

//     this->getRenderer()->AddActor2D( m_bottomOrientationMarker );

    updateAnnotations();
}

void Q2DViewer::updateAnnotations()
{
    if( m_enabledAnnotations && Q2DViewer::ReferenceAnnotation )
    {
        m_bottomOrientationMarker->VisibilityOn();
        m_sideOrientationMarker->VisibilityOn();
        m_patientOrientationTextActor[0]->VisibilityOn();
        m_patientOrientationTextActor[1]->VisibilityOn();
        m_patientOrientationTextActor[2]->VisibilityOn();
        m_patientOrientationTextActor[3]->VisibilityOn();
    }
    else
    {
        m_bottomOrientationMarker->VisibilityOff();
        m_sideOrientationMarker->VisibilityOff();
        m_patientOrientationTextActor[0]->VisibilityOff();
        m_patientOrientationTextActor[1]->VisibilityOff();
        m_patientOrientationTextActor[2]->VisibilityOff();
        m_patientOrientationTextActor[3]->VisibilityOff();
    }

    if( m_enabledAnnotations && Q2DViewer::WindowLevelAnnotation )
    {
        m_textAnnotation->VisibilityOn();
    }
    else
    {
        m_textAnnotation->VisibilityOff();
    }

}

void Q2DViewer::initInformationText()
{
    m_lowerLeftText = tr("Slice: %1/%2")
                .arg( m_currentSlice )
                .arg( m_size[2] );
                
    m_upperLeftText = tr("Image Size: %1 x %2\nView Size: %3 x %4\nWW: %5 WL: %6 ")
                .arg( m_size[0] )
                .arg( m_size[1] )
                .arg( m_viewer->GetRenderWindow()->GetSize()[0] )
                .arg( m_viewer->GetRenderWindow()->GetSize()[1] )
                .arg( m_viewer->GetColorWindow() )
                .arg( m_viewer->GetColorLevel() );

    QString institution = "institution i.e. IDI-GIRONA";
    QString patientName = "patient name here";
    QString patientID = "patient ID here";
    QString date = "00/00/3000";

    m_upperRightText = tr("%1\n%2\n%3\n%4")
                .arg( institution )
                .arg( patientName )
                .arg( patientID )
                .arg( date );
                
    m_lowerRightText = tr("Made by Starviewer %1").arg( QChar(169) );
    
    m_textAnnotation->SetText( 0 , m_lowerLeftText.toAscii() );
    m_textAnnotation->SetText( 1 , m_lowerRightText.toAscii() );
    m_textAnnotation->SetText( 2 , m_upperLeftText.toAscii() );
    m_textAnnotation->SetText( 3 , m_upperRightText.toAscii() );
    
    m_textAnnotation->SetImageActor( m_viewer->GetImageActor() );
    m_textAnnotation->SetWindowLevel( m_viewer->GetWindowLevel() );
    m_textAnnotation->ShowSliceAndImageOn();

//  \TODO posar b� le texte que sempre es mostri a una mida raonable i que no faci els canvis "raros"
//     m_textAnnotation->GetTextProperty()->SetFontSize( 80 );
//     m_textAnnotation->GetTextProperty()->ItalicOn();
    
    m_viewer->GetRenderer()->AddActor2D( m_textAnnotation );
    
}

void Q2DViewer::displayInformationText( bool display )
{
    if( display )
    {
        m_textAnnotation->VisibilityOn();
        connect( this , SIGNAL( infoChanged() ) , this , SLOT( updateInformationText() ) );
    }
    else
    {
        m_textAnnotation->VisibilityOff();
        disconnect( this , SIGNAL( infoChanged() ) , this , SLOT( updateInformationText() ) );
    }
}

void Q2DViewer::anyEvent()
{
    // std::cout << "any event " << std::endl; 
}

void Q2DViewer::onMouseMove()
{/*
    vtkRenderWindowInteractor* interactor = m_vtkWidget->GetRenderWindow()->GetInteractor();
    // agafem el punt que est� apuntant el ratol� en aquell moment
    m_cellPicker->Pick( interactor->GetEventPosition()[0],
                interactor->GetEventPosition()[1], 
                m_viewer->GetSlice(), 
                m_viewer->GetRenderer()
                );
    
    // calculem el pixel trobat
    double tolerance;
    int subId;
    double pointCoordinates[3], weights[8];
    double q[3], imageValue;
    
    interactor->GetPicker()->GetPickPosition( q );

    this->m_modelPointFromCursor.setValues( q );

    vtkPointData *pointData = m_mainVolume->getVtkData()->GetPointData();
    vtkPointData* outPointData = vtkPointData::New();
    outPointData->InterpolateAllocate( pointData , 1 , 1 );

    // Use tolerance as a function of size of source data
    tolerance = m_mainVolume->getVtkData()->GetLength();
    tolerance = tolerance ? tolerance*tolerance / 1000.0 : 0.001;

    // Find the cell that contains q and get it
    vtkCell *cell = m_mainVolume->getVtkData()->FindAndGetCell( q , NULL , -1 , tolerance , subId , pointCoordinates , weights );
    int found = 0;
    if ( cell )
    {
        // Interpolate the point data
        outPointData->InterpolatePoint( pointData , 0 , cell->PointIds , weights );
        imageValue = outPointData->GetScalars()->GetTuple1(0);
        found = 1;
    }
    outPointData->Delete();
    if( !found )
        updateCursor( -1, -1, -1, -1 );
    else
        updateCursor( q[0], q[1], q[2], imageValue );
    */    
    updateWindowLevelAnnotation();
    this->getInteractor()->Render();

}

void Q2DViewer::onLeftButtonDown()
{
    switch( m_currentTool )
    {

    case Q2DViewer::Manipulate:
        if( m_manipulateState == Q2DViewer::Ready )
        {
            // trobar els actors
            int x = getInteractor()->GetEventPosition()[0];
            int y = getInteractor()->GetEventPosition()[1];

            // Okay, make sure that the pick is in the current renderer
            if ( !this->getRenderer() || !this->getRenderer()->IsInViewport( x, y ) )
            {
//                 this->State = vtkImagePlaneWidget::Outside;
                return;
            }
            // Okay, we can process this. If anything is picked, then we
            // can start pushing or check for adjusted states.
            vtkAssemblyPath *path;
            m_cellPicker->Pick( x , y , 0.0 , this->getRenderer() );
            path = m_cellPicker->GetPath();
            
/*            int found = 0;
            int i;
            if ( path != 0 )
            {
                // Deal with the possibility that we may be using a shared picker
                vtkCollectionSimpleIterator sit;
                path->InitTraversal(sit);
                vtkAssemblyNode *node;
                for( i = 0; i < path->GetNumberOfItems() && !found ;i++ )
                {
                    node = path->GetNextNode(sit);
                    if( node->GetViewProp() == vtkProp::SafeDownCast(this->TexturePlaneActor) )
                    {
                        found = 1;
                    }
                }
            }*/
            
            if ( /* !found ||*/ path == 0 )
            {
//                 this->State = vtkImagePlaneWidget::Outside;
//                 this->HighlightPlane(0);
//                 this->ActivateMargins(0);
                return;
            }
            else
            {
//                 this->State = vtkImagePlaneWidget::Pushing;
//                 this->HighlightPlane(1);
//                 this->ActivateMargins(1);
//                 this->AdjustState();
//                 this->UpdateMargins();
            }
            
//             this->EventCallbackCommand->SetAbortFlag(1);
//             this->StartInteraction();
//             this->InvokeEvent(vtkCommand::StartInteractionEvent,0);
//             this->Interactor->Render();
            getInteractor()->Render();
            
        }
    break;

    default:
    break;
    
    }
//     switch( m_leftButtonAction )
//     {
//     case Q2DViewer::CursorAction:
//         startCursor();
//     break;
//     
//     case Q2DViewer::SliceMotionAction:
//         startSliceMotion();
//     break;
// 
//     case Q2DViewer::WindowLevelAction:
//         startWindowLevel();
//     break;
//     }
}

void Q2DViewer::onLeftButtonUp()
{
    switch( m_leftButtonAction )
    {
    case Q2DViewer::CursorAction:
        stopCursor();
    break;
    
    case Q2DViewer::SliceMotionAction:
        stopSliceMotion();
    break;

    case Q2DViewer::WindowLevelAction:
        stopWindowLevel();
    break;
    }
}

void Q2DViewer::onMiddleButtonDown()
{
    switch( m_middleButtonAction )
    {
    case Q2DViewer::CursorAction:
        startCursor();
    break;
    
    case Q2DViewer::SliceMotionAction:
        startSliceMotion();
    break;

    case Q2DViewer::WindowLevelAction:
        startWindowLevel();
    break;
    }
}

void Q2DViewer::onMiddleButtonUp()
{
    switch( m_middleButtonAction )
    {
    case Q2DViewer::CursorAction:
        stopCursor();
    break;
    
    case Q2DViewer::SliceMotionAction:
        stopSliceMotion();
    break;

    case Q2DViewer::WindowLevelAction:
        stopWindowLevel();
    break;
    }
}

void Q2DViewer::onRightButtonDown()
{
    switch( m_rightButtonAction )
    {
    case Q2DViewer::CursorAction:
        startCursor();
    break;
    
    case Q2DViewer::SliceMotionAction:
        startSliceMotion();
    break;

    case Q2DViewer::WindowLevelAction:
        startWindowLevel();
    break;
    }
}

void Q2DViewer::onRightButtonUp()
{
    switch( m_rightButtonAction )
    {
    case Q2DViewer::CursorAction:
        stopCursor();
    break;
    
    case Q2DViewer::SliceMotionAction:
        stopSliceMotion();
    break;

    case Q2DViewer::WindowLevelAction:
        stopWindowLevel();
    break;
    }
}

void Q2DViewer::startCursor()
{
}

void Q2DViewer::startSliceMotion()
{
}

void Q2DViewer::startWindowLevel()
{
}

void Q2DViewer::stopCursor()
{
}

void Q2DViewer::stopSliceMotion()
{
}

void Q2DViewer::stopWindowLevel()
{
}

void Q2DViewer::eventHandler( vtkObject *obj, unsigned long event, void *client_data, void * call_data, vtkCommand *command )
{
    // el primer que s'hauria de fer �s executar l'acci� que es faci en aquell estat indistintament de la tool com �s el mostrar en la pantalla el valor del pixel actual
   anyEvent();
//    std::cout << vtkCommand::GetStringFromEventId( event ) << std::endl;
    // fer el que calgui per cada tipus d'event

    switch( event )
    {
    
    case vtkCommand::MouseMoveEvent:
        onMouseMove();
        //  \TODO aix� �s una cutrada per� s'hauria de veure com connectar l'event de vtk perk ens indiqui quan canvia realment el window level
    emit windowLevelChanged( m_viewer->GetColorWindow() , m_viewer->GetColorLevel() );
    break;

    case vtkCommand::LeftButtonPressEvent:
        m_lastButtonPressed = Q2DViewer::LeftButton;
        onLeftButtonDown();
    break;

    case vtkCommand::LeftButtonReleaseEvent:
        m_lastButtonPressed = Q2DViewer::LeftButton;
        onLeftButtonUp();
    break;

    case vtkCommand::MiddleButtonPressEvent:
        m_lastButtonPressed = Q2DViewer::MiddleButton;
        onMiddleButtonDown();
    break;

    case vtkCommand::MiddleButtonReleaseEvent:
        m_lastButtonPressed = Q2DViewer::MiddleButton;
        onMiddleButtonUp();
    break;
    
    case vtkCommand::RightButtonPressEvent:
        m_lastButtonPressed = Q2DViewer::RightButton;
        onRightButtonDown();
    break;

    case vtkCommand::RightButtonReleaseEvent:
        m_lastButtonPressed = Q2DViewer::RightButton;
        onRightButtonUp();
    break;

//     case vtkCommand::WindowLevelEvent:
//         emit windowLevelChanged( m_viewer->GetColorWindow() , m_viewer->GetColorLevel() );
    break;

    default:
    break;
    
    }
    
    // ara de mentres ser� aix� segons la tool activa analitzarem uns events o uns altres
    switch( m_currentTool )
    {
    // ----------------------------------------------------
    // ZOOM
    // ----------------------------------------------------
    case Zoom:
    break;
    
    // ----------------------------------------------------
    // PICK
    // ----------------------------------------------------
    case Pick:
        //
        // estats?
        // Si l'event �s bot� esquerre pressionat -> escollir seed candidata : mostrar valor en pantalla
        // ""                            ""        + MouseMove : mostrar valor en pantalla
        // ""                          released -> confirmar seed -> posar-la a la llista , afegir punt en pantalla
    break;
    
    // ----------------------------------------------------
    // DISTANCE
    // ----------------------------------------------------
    case Distance:   
        m_distanceTool->dispatchEvent( DistanceTool::vtkCommandEventToToolEvent( event ) );
    break;
    
    // ----------------------------------------------------
    // CURSOR : mostra cursor sobre la imatge
    // ----------------------------------------------------
    case Cursor:
        // estats?
        // si mouseMove , mostrar cursor sobre el punt en el que estem
    break;

    // ----------------------------------------------------
    // MANIPULATE
    // ----------------------------------------------------
    case Manipulate:
        // aquesta tool el que fa �s es que podem manipular els elements adicionals de l'escena, per tant hi ha dos passos
        // 1) Escollir/Agafar l'actor. Quan es cliqui el mouse farem un assembly path. Si hi ha algun actor llavors es podr� manipular
        // 2) En cas que hi hagi actor/s mentre el bot� continui pitjat farem la manipulaci� que toqui (despla�ar, spin, rotar, etc)
        if( event == vtkCommand::LeftButtonPressEvent && m_manipulateState == Q2DViewer::Ready )
        {
            // trobar si hi ha algun objecte all� on hem clicat
        }
        if( event == vtkCommand::MouseMoveEvent && m_manipulateState == Q2DViewer::Picked )
        
        if( event == vtkCommand::LeftButtonReleaseEvent )
        
    break;
    // ----------------------------------------------------
    // DEFAULT
    // ----------------------------------------------------
    default:
    break;
    }
 
}


void Q2DViewer::contextMenuRelease( vtkObject* object , unsigned long event, void *client_data, vtkCommand *command )
{
    // Extret dels exemples de vtkEventQtSlotConnect
    // get interactor
    vtkRenderWindowInteractor* iren = vtkRenderWindowInteractor::SafeDownCast(object);
    // consume event so the interactor style doesn't get it
    command->AbortFlagOn();
    // Obtenim la posici� de l'event (moure el mouse, en aquest cas)
    int eventPosition[2];
    iren->GetEventPosition( eventPosition );
    int* size = iren->GetSize();
    // remember to flip y
    QPoint pt = QPoint( eventPosition[0], size[1]-eventPosition[1]);

    // aquesta posici� no �s del tot bona ja que no s�n les coordenades globals, sin o de finestra
    QMenu contextMenu( this );
    contextMenu.addAction( m_resetAction );
    
    // map to global
    QPoint global_pt = contextMenu.parentWidget()->mapToGlobal( pt );
    contextMenu.exec( global_pt );
}

void Q2DViewer::setupInteraction()
{   
    // configurem l'Image Viewer i el qvtkWidget
    // aquesta crida obliga a que hi hagi un input abans, sin� el pipeline del vtkImageViewer ens d�na error perqu� no t� cap actor creat \TODO aquesta crida hauria d'anar aqu� o nom�s despr�s del primer setInput?
    m_vtkWidget->SetRenderWindow( m_viewer->GetRenderWindow() );
    m_viewer->SetupInteractor( m_vtkWidget->GetRenderWindow()->GetInteractor() );

    m_vtkWidget->GetRenderWindow()->GetInteractor()->SetPicker( m_cellPicker );
    
    m_vtkQtConnections = vtkEventQtSlotConnect::New();

// men� contextual
//     m_vtkQtConnections->Connect( m_vtkWidget->GetRenderWindow()->GetInteractor(),
//                       QVTKWidget::ContextMenuEvent,//vtkCommand::RightButtonPressEvent,
//                        this,
//                        SLOT( contextMenuRelease(vtkObject*,unsigned long,void*, vtkCommand *) ) );

    // despatxa qualsevol event-> tools                       
    m_vtkQtConnections->Connect( m_vtkWidget->GetRenderWindow()->GetInteractor(), vtkCommand::AnyEvent, this, SLOT( eventHandler(vtkObject*,unsigned long,void *,void *, vtkCommand *) ) );
    
}

void Q2DViewer::setInput( Volume* volume )
{
    if( volume == 0 )
        return;
    m_mainVolume = volume;    
    m_viewer->SetInput( m_mainVolume->getVtkData() );

    // fem update de les mides dels indicadors de refer�ncia
    m_sideOrientationMarker->GetPositionCoordinate()->SetCoordinateSystemToWorld();
    m_sideOrientationMarker->GetPosition2Coordinate()->SetCoordinateSystemToWorld();
    m_sideOrientationMarker->SetPosition(  0.9 , 0.7 );
    m_sideOrientationMarker->SetPosition2( 0.9 , 0.2 );
    m_sideOrientationMarker->SetRange( 200 , 5000 );

    m_bottomOrientationMarker->GetPositionCoordinate()->SetCoordinateSystemToWorld();
    m_bottomOrientationMarker->GetPosition2Coordinate()->SetCoordinateSystemToWorld();
    m_bottomOrientationMarker->SetPosition(  0.9 , 0.7 );
    m_bottomOrientationMarker->SetPosition2( 0.9 , 0.2 );
    m_bottomOrientationMarker->SetRange( 200 , 5000 );

    m_mainVolume->getDimensions( m_size );
    // \TODO s'ha de cridar cada cop que posem dades noves o nom�s el primer cop?
    setupInteraction();

}

void Q2DViewer::setOverlayInput( Volume* volume )
{
    m_overlayVolume = volume;
    
    vtkImageCheckerboard* imageCheckerBoard = vtkImageCheckerboard::New();
    vtkLookupTable* table = vtkLookupTable::New();
    vtkImageMapToColors* mapColors = vtkImageMapToColors::New();
    vtkImageBlend* blender = vtkImageBlend::New();
    
    vtkImageRectilinearWipe *wipe = vtkImageRectilinearWipe::New();
    
    switch( m_overlay )
    {
    case CheckerBoard:
        
        imageCheckerBoard->SetInput1( m_mainVolume->getVtkData() );
        imageCheckerBoard->SetInput2( m_overlayVolume->getVtkData() );
        imageCheckerBoard->SetNumberOfDivisions( m_divisions );
        // actualitzem el viewer
        m_viewer->SetInputConnection( imageCheckerBoard->GetOutputPort() ); // li donem el m_imageCheckerboard com a input
    break;
    
    case Blend:
        // first build the lookup table
        
        table->SetValueRange(0,1);
        table->SetSaturationRange(0,0);
        table->SetAlphaRange(1,1);
        table->SetRampToLinear();
        table->Build();
        //  set alpha of first value to 0 instead of 1
        table->SetTableValue(0, 0,0,0,0);
        // 
        mapColors->SetLookupTable( table );
        mapColors->SetInput( m_mainVolume->getVtkData() );
    
        // creem el blender
        blender->SetInput( 0 , m_mainVolume->getVtkData()/*mapColors->GetOutput()*/ );
        blender->SetInput( 1 , m_overlayVolume->getVtkData() );
        blender->SetOpacity( 0, 0.5 );
        blender->SetOpacity( 1, 0.5 );
    // actualitzem el viewer
        m_viewer->SetInputConnection( blender->GetOutputPort() ); // li donem el blender com a input
    break;
    
    case RectilinearWipe:
        wipe->SetInput( 0 , m_mainVolume->getVtkData() );
        wipe->SetInput( 1 , m_overlayVolume->getVtkData() );
        wipe->SetPosition(20,20);
        wipe->SetWipeToUpperLeft();        
        m_viewer->SetInput( wipe->GetOutput() );
    break;    
    
    }
}

void Q2DViewer::render()
{
    // si tenim dades
    if( m_mainVolume )
    {        
        // li donem el window/level correcte
        resetWindowLevelToDefault();
        // Aix� �s necessari perqu� la imatge es rescali a les mides de la finestreta
        m_viewer->GetRenderer()->ResetCamera();
        updateView();
    }
    // mostrar error/av�s si no hi ha dades per visualitzar?
}

void Q2DViewer::setView( ViewType view )
{    
    m_lastView = view;
}

void Q2DViewer::updateView()
{
    switch( m_lastView )
    {
    case Axial:
        m_viewer->SetSliceOrientationToXY();
        m_size[0] = m_mainVolume->getDimensions()[0];
        m_size[1] = m_mainVolume->getDimensions()[1];
        m_size[2] = m_mainVolume->getDimensions()[2];
    break;
    case Sagittal:
        m_viewer->SetSliceOrientationToYZ();
        //\TODO hauria de ser a partir de main_volume o a partir de l'output del viewer
        m_size[0] = m_mainVolume->getDimensions()[1];
        m_size[1] = m_mainVolume->getDimensions()[2];
        m_size[2] = m_mainVolume->getDimensions()[0];
    break;
    case Coronal:
        m_viewer->SetSliceOrientationToXZ();
        //\TODO hauria de ser a partir de main_volume o a partir de l'output del viewer
        m_size[0] = m_mainVolume->getDimensions()[0];
        m_size[1] = m_mainVolume->getDimensions()[2];
        m_size[2] = m_mainVolume->getDimensions()[1];
    break;
    default:
    // podem posar en Axial o no fer res
        m_viewer->SetSliceOrientationToXY();
    break;
    }
    // cada cop que canviem de llesca posarem per defecte la llesca del mig d'aquella vista
    setSlice( m_viewer->GetSliceRange()[1]/2 );
    updateWindowSizeAnnotation();
}

void Q2DViewer::setSlice( int value )
{
    m_currentSlice = value;
    emit sliceChanged( m_currentSlice );

    if( m_currentSlice <= m_viewer->GetSliceRange()[1] && m_currentSlice >= m_viewer->GetSliceRange()[0] )
    {
        m_viewer->SetSlice( m_currentSlice );
    }
    updateSliceAnnotation();
}

void Q2DViewer::resizeEvent( QResizeEvent *resize )
{
    updateWindowSizeAnnotation();
}

void Q2DViewer::setWindowLevel( double window , double level )
{
    if( m_viewer && m_mainVolume )
    {
        m_viewer->SetColorLevel( level );
        m_viewer->SetColorWindow( window );
        updateWindowLevelAnnotation();
        getInteractor()->Render();
    }
}

void Q2DViewer::resetWindowLevelToDefault()
{
// aix� ens d�na un level/level "maco" per defecte
// situem el level al mig i donem un window complet de tot el rang
    if( m_mainVolume )
    {
        double * range = m_mainVolume->getVtkData()->GetScalarRange();
        double window = fabs(range[1] - range[0]);
        double level = ( range[1] + range[0] )/ 2.0;
        m_viewer->SetColorLevel( level );
        m_viewer->SetColorWindow( window );
        this->getInteractor()->Render();

        emit windowLevelChanged( m_viewer->GetColorWindow() , m_viewer->GetColorLevel() );
        updateWindowLevelAnnotation();
    }
    // mostrar av�s/error si no hi ha volum?
}

void Q2DViewer::resetWindowLevelToBone()
{
    if( m_mainVolume )
    {
        double * range = m_mainVolume->getVtkData()->GetScalarRange();
        // ajustem el rang
        double window = fabs(range[1] - range[0] ) * 0.3;
        // situem al punt correcte el level
        double level = ( range[1] + range[0] ) * 0.8;
        m_viewer->SetColorLevel( level );
        m_viewer->SetColorWindow( window );

        this->getInteractor()->Render();
        
        emit windowLevelChanged( m_viewer->GetColorWindow() , m_viewer->GetColorLevel() );
        updateWindowLevelAnnotation();
    }
}

void Q2DViewer::resetWindowLevelToSoftTissue()
{
    if( m_mainVolume )
    {
        double * range = m_mainVolume->getVtkData()->GetScalarRange();
        // ajustem el rang
        double window = fabs(range[1] - range[0] ) *  0.02;
        // situem al punt correcte el level
        double level = ( range[1] + range[0] ) * 0.53;
        m_viewer->SetColorLevel( level );
        m_viewer->SetColorWindow( window );

        this->getInteractor()->Render();

        emit windowLevelChanged( m_viewer->GetColorWindow() , m_viewer->GetColorLevel() );
        updateWindowLevelAnnotation();
    }
}

void Q2DViewer::resetWindowLevelToFat()
{
    if( m_mainVolume )
    {
        double * range = m_mainVolume->getVtkData()->GetScalarRange();
        // ajustem el rang
        double window = fabs(range[1] - range[0] ) * 0.02;
        // situem al punt correcte el level
        double level = ( range[1] + range[0] ) * 0.46;
        m_viewer->SetColorLevel( level );
        m_viewer->SetColorWindow( window );

        this->getInteractor()->Render();

        emit windowLevelChanged( m_viewer->GetColorWindow() , m_viewer->GetColorLevel() );
        updateWindowLevelAnnotation();
    }
}

void Q2DViewer::resetWindowLevelToLung()
{
    if( m_mainVolume )
    {
        double * range = m_mainVolume->getVtkData()->GetScalarRange();
        // ajustem el rang
        double window = fabs(range[1] - range[0] ) * 0.1;
        // situem al punt correcte el level
        double level = ( range[1] + range[0] ) * 0.25;
        m_viewer->SetColorLevel( level );
        m_viewer->SetColorWindow( window );

        this->getInteractor()->Render();

        emit windowLevelChanged( m_viewer->GetColorWindow() , m_viewer->GetColorLevel() );
        updateWindowLevelAnnotation();   
    }
}

void Q2DViewer::updateWindowLevelAnnotation()
{
    m_upperLeftText = tr("Image Size: %1 x %2\nView Size: %3 x %4\nWW: %5 WL: %6 ")
                .arg( m_size[0] )
                .arg( m_size[1] )
                .arg( m_viewer->GetRenderWindow()->GetSize()[0] )
                .arg( m_viewer->GetRenderWindow()->GetSize()[1] )
                .arg( m_viewer->GetColorWindow() )
                .arg( m_viewer->GetColorLevel() );
    m_textAnnotation->SetText( 2 , m_upperLeftText.toAscii() );    
}

void Q2DViewer::updateSliceAnnotation()
{
    m_lowerLeftText = tr("Slice: %1/%2")
                .arg( m_currentSlice )
                .arg( m_size[2] );
    m_textAnnotation->SetText( 0 , m_lowerLeftText.toAscii() );
}

void Q2DViewer::updateWindowSizeAnnotation()
{
    m_upperLeftText = tr("Image Size: %1 x %2\nView Size: %3 x %4\nWW: %5 WL: %6 ")
                .arg( m_size[0] )
                .arg( m_size[1] )
                .arg( m_viewer->GetRenderWindow()->GetSize()[0] )
                .arg( m_viewer->GetRenderWindow()->GetSize()[1] )
                .arg( m_viewer->GetColorWindow() )
                .arg( m_viewer->GetColorLevel() );
    m_textAnnotation->SetText( 2 , m_upperLeftText.toAscii() );
}
    
void Q2DViewer::setDivisions( int x , int y , int z )
{
    m_divisions[0] = x;
    m_divisions[1] = y;
    m_divisions[2] = z;
}

void Q2DViewer::setDivisions( int data[3] )
{
    m_divisions[0] = data[0];
    m_divisions[1] = data[1];
    m_divisions[2] = data[2];
}

int* Q2DViewer::getDivisions( void )
{
    return m_divisions;
}

void Q2DViewer::getDivisions( int data[3] )
{
    data[0] = m_divisions[0];
    data[1] = m_divisions[1];
    data[2] = m_divisions[2];
}

void Q2DViewer::saveAll( const char *baseName , FileType extension )
{
    switch( extension )
    {
    case PNG:
    break;
    
    case JPEG:
    break;

    case TIFF:
    break;
     
    case DICOM:
    break;

    case META:
    break;
    
    case PNM:
    break;
    }
}

void Q2DViewer::saveCurrent( const char *baseName , FileType extension )
{
    m_windowToImageFilter->Update();
    m_windowToImageFilter->Modified();
    vtkImageData *image = m_windowToImageFilter->GetOutput();
    switch( extension )
    {
    case PNG:
        vtkImageWriter *pngWriter = vtkPNGWriter::New();
        pngWriter->SetInput( image );
        pngWriter->SetFilePattern( "%s-%d.png" );
        pngWriter->SetFilePrefix( baseName );
        pngWriter->Write();
    break;
    
    case JPEG:
        vtkImageWriter *jpegWriter = vtkJPEGWriter::New();
        jpegWriter->SetInput( image );
        jpegWriter->SetFilePattern( "%s-%d.jpg" );
        jpegWriter->SetFilePrefix( baseName );
        jpegWriter->Write();
    break;
// \TODO el format tiff fa petar al desar, mirar si �s problema de compatibilitat del sistema o de les pr�pies vtk
    case TIFF:
        vtkImageWriter *tiffWriter = vtkTIFFWriter::New();
        tiffWriter->SetInput( image );
        tiffWriter->SetFilePattern( "%s-%d.tif" );
        tiffWriter->SetFilePrefix( baseName );
        tiffWriter->Write();
    break;

    case PNM:
        vtkImageWriter *pnmWriter = vtkPNMWriter::New();
        pnmWriter->SetInput( image );
        pnmWriter->SetFilePattern( "%s-%d.pnm" );
        pnmWriter->SetFilePrefix( baseName );
        pnmWriter->Write();
    break;

    case DICOM:
    break;

    case META:
    break;
    }

}

void Q2DViewer::setVtkLUT( vtkLookupTable *lut )
{
    m_viewer->GetWindowLevel()->SetLookupTable( lut );
}

vtkLookupTable *Q2DViewer::getVtkLUT( )
{
    return vtkLookupTable::SafeDownCast( m_viewer->GetWindowLevel()->GetLookupTable() );
}

};  // end namespace udg 
