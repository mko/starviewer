#include "keyimagenotetoolwidget.h"
#include "keyimagenotedisplayer.h"
#include "keyimagenote.h"
#include "keyimagenotemanager.h"
#include "series.h"

namespace udg {

KeyImageNoteToolWidget::KeyImageNoteToolWidget(QWidget *parent): QDialog(parent)
{
    setupUi(this);
    createConnections();
}

KeyImageNoteToolWidget::~KeyImageNoteToolWidget()
{

}

void KeyImageNoteToolWidget::setKeyImageNotes(QList<KeyImageNote*> keyImageNotesOfImage)
{
    m_keyImageNotesOfImage = keyImageNotesOfImage;
    fillKeyImageNoteSelectorComboBox();
}

void KeyImageNoteToolWidget::fillKeyImageNoteSelectorComboBox()
{
    m_keyImageNoteSelectorComboBox->clear();
    foreach (KeyImageNote *keyImageNote, m_keyImageNotesOfImage)
    {
        m_keyImageNoteSelectorComboBox->addItem(tr("Key Image Note:") + " " + keyImageNote->getInstanceNumber() + " " + tr("Series:") + " " + keyImageNote->getParentSeries()->getSeriesNumber() + " " + tr("Title:") + " " + KeyImageNote::getDocumentTitleAsString(keyImageNote->getDocumentTitle()));
    }
    
    m_keyImageNoteSelectorComboBox->setCurrentIndex(0);

}

void KeyImageNoteToolWidget::createConnections()
{
    connect(m_keyImageNoteSelectorComboBox, SIGNAL(currentIndexChanged(int)), SLOT(changeKeyImageNoteDisplayed(int)));
    connect(m_keyImageNoteDisplayer->m_thumbnailImageDisplayer, SIGNAL(show(const QString &)), SLOT(close()));
    connect(m_keyImageNoteDisplayer->m_showButton, SIGNAL(clicked()), SLOT(close()));
}

void KeyImageNoteToolWidget::changeKeyImageNoteDisplayed(int index)
{
    if (index >= 0 && index < m_keyImageNotesOfImage.count())
    {
        m_keyImageNoteDisplayer->setKeyImageNote(m_keyImageNotesOfImage.at(index));
    }
}

void KeyImageNoteToolWidget::setKeyImageNoteManager(KeyImageNoteManager *keyImageNoteManager)
{
    m_keyImageNoteManager = keyImageNoteManager;
    m_keyImageNoteDisplayer->setKeyImageNoteManager(m_keyImageNoteManager);
}

}