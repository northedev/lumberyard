/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
// Original file Copyright Crytek GMBH or its affiliates, used under license.

#include <AudioControlsEditorWindow.h>

#include <AzFramework/StringFunc/StringFunc.h>
#include <ATLControlsModel.h>
#include <ATLControlsPanel.h>
#include <AudioControlsEditorPlugin.h>
#include <AudioControlsEditorUndo.h>
#include <AudioSystemPanel.h>
#include <CryFile.h>
#include <CryPath.h>
#include <DockTitleBarWidget.h>
#include <IAudioSystem.h>
#include <ImplementationManager.h>
#include <InspectorPanel.h>
#include <ISystem.h>
#include <QAudioControlEditorIcons.h>
#include <Util/PathUtil.h>

#include <QPaintEvent>
#include <QPushButton>
#include <QApplication>
#include <QPainter>
#include <QMessageBox>

namespace AudioControls
{
    //-------------------------------------------------------------------------------------------//
    // static
    bool CAudioControlsEditorWindow::m_wasClosed = false;

    //-------------------------------------------------------------------------------------------//
    CAudioControlsEditorWindow::CAudioControlsEditorWindow()
    {
        setupUi(this);

        m_pATLModel = CAudioControlsEditorPlugin::GetATLModel();
        IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetAudioSystemEditorImpl();
        if (pAudioSystemImpl)
        {
            m_pATLControlsPanel = new CATLControlsPanel(m_pATLModel, CAudioControlsEditorPlugin::GetControlsTree());
            m_pInspectorPanel = new CInspectorPanel(m_pATLModel);
            m_pAudioSystemPanel = new CAudioSystemPanel();

            CDockTitleBarWidget* pTitleBar = new CDockTitleBarWidget(m_pATLControlsDockWidget);
            m_pATLControlsDockWidget->setTitleBarWidget(pTitleBar);

            pTitleBar = new CDockTitleBarWidget(m_pInspectorDockWidget);
            m_pInspectorDockWidget->setTitleBarWidget(pTitleBar);

            pTitleBar = new CDockTitleBarWidget(m_pMiddlewareDockWidget);
            m_pMiddlewareDockWidget->setTitleBarWidget(pTitleBar);

            // Custom title based on Middleware name
            m_pMiddlewareDockWidget->setWindowTitle(QString(pAudioSystemImpl->GetName().c_str()) + " Controls");

            m_pATLControlsDockLayout->addWidget(m_pATLControlsPanel);
            m_pInspectorDockLayout->addWidget(m_pInspectorPanel);
            m_pMiddlewareDockLayout->addWidget(m_pAudioSystemPanel);

            Update();

            connect(m_pATLControlsPanel, SIGNAL(SelectedControlChanged()), this, SLOT(UpdateInspector()));
            connect(m_pATLControlsPanel, SIGNAL(SelectedControlChanged()), this, SLOT(UpdateFilterFromSelection()));
            connect(m_pATLControlsPanel, SIGNAL(ControlTypeFiltered(EACEControlType, bool)), this, SLOT(FilterControlType(EACEControlType, bool)));

            connect(CAudioControlsEditorPlugin::GetImplementationManager(), SIGNAL(ImplementationChanged()), this, SLOT(Update()));

            connect(&m_fileSystemWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(ReloadMiddlewareData()));

            GetIEditor()->RegisterNotifyListener(this);

            // LY-11309: this call to reload middleware data will force refresh of the data when
            // making changes to the middleware project while the AudioControlsEditor window is closed.
            if (m_wasClosed)
            {
                ReloadMiddlewareData();
            }
        }
    }

    //-------------------------------------------------------------------------------------------//
    CAudioControlsEditorWindow::~CAudioControlsEditorWindow()
    {
        GetIEditor()->UnregisterNotifyListener(this);
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::StartWatchingFolder(const AZStd::string_view folder)
    {
        m_fileSystemWatcher.addPath(folder.data());

        AZStd::string search;
        AzFramework::StringFunc::Path::Join(folder.data(), "*.*", search, false, true, false);
        _finddata_t fd;
        ICryPak* pCryPak = gEnv->pCryPak;
        intptr_t handle = pCryPak->FindFirst(search.c_str(), &fd);
        if (handle != -1)
        {
            do
            {
                AZStd::string sName = fd.name;
                if (!sName.empty() && sName[0] != '.')
                {
                    if (fd.attrib & _A_SUBDIR)
                    {
                        AzFramework::StringFunc::Path::Join(folder.data(), sName.c_str(), sName);
                        StartWatchingFolder(sName);
                    }
                }
            }
            while (pCryPak->FindNext(handle, &fd) >= 0);
            pCryPak->FindClose(handle);
        }
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::keyPressEvent(QKeyEvent* pEvent)
    {
        uint16 mod = pEvent->modifiers();
        if (pEvent->key() == Qt::Key_S && pEvent->modifiers() == Qt::ControlModifier)
        {
            Save();
        }
        else if (pEvent->key() == Qt::Key_Z && (pEvent->modifiers() & Qt::ControlModifier))
        {
            if (pEvent->modifiers() & Qt::ShiftModifier)
            {
                GetIEditor()->Redo();
            }
            else
            {
                GetIEditor()->Undo();
            }
        }
        QMainWindow::keyPressEvent(pEvent);
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::closeEvent(QCloseEvent* pEvent)
    {
        if (m_pATLModel && m_pATLModel->IsDirty())
        {
            QMessageBox messageBox(this);
            messageBox.setText(tr("There are unsaved changes."));
            messageBox.setInformativeText(tr("Do you want to save your changes?"));
            messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            messageBox.setDefaultButton(QMessageBox::Save);
            messageBox.setWindowTitle("Audio Controls Editor");
            switch (messageBox.exec())
            {
            case QMessageBox::Save:
                QApplication::setOverrideCursor(Qt::WaitCursor);
                Save();
                QApplication::restoreOverrideCursor();
                pEvent->accept();
                break;
            case QMessageBox::Discard:
                pEvent->accept();
                break;
            default:
                pEvent->ignore();
                return;
            }
        }
        else
        {
            pEvent->accept();
        }

        // If the close event was accepted, set this flag so next time the window opens it will refresh the middleware data.
        m_wasClosed = true;
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::Reload()
    {
        bool bReload = true;
        if (m_pATLModel && m_pATLModel->IsDirty())
        {
            QMessageBox messageBox(this);
            messageBox.setText(tr("If you reload you will lose all your unsaved changes."));
            messageBox.setInformativeText(tr("Are you sure you want to reload?"));
            messageBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            messageBox.setDefaultButton(QMessageBox::No);
            messageBox.setWindowTitle("Audio Controls Editor");
            bReload = (messageBox.exec() == QMessageBox::Yes);
        }

        if (bReload)
        {
            CAudioControlsEditorPlugin::ReloadModels();
            Update();
        }
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::Update()
    {
        if (!m_pATLControlsPanel)
        {
            return;
        }

        m_pATLControlsPanel->Reload();
        m_pAudioSystemPanel->Reload();
        UpdateInspector();
        IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetAudioSystemEditorImpl();
        if (pAudioSystemImpl)
        {
            StartWatchingFolder(pAudioSystemImpl->GetDataPath());
            m_pMiddlewareDockWidget->setWindowTitle(QString(pAudioSystemImpl->GetName().c_str()) + " Controls");
        }
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::Save()
    {
        bool bPreloadsChanged = m_pATLModel->IsTypeDirty(eACET_PRELOAD);
        CAudioControlsEditorPlugin::SaveModels();
        UpdateAudioSystemData();

        // if preloads have been modified, ask the user if s/he wants to refresh the audio system
        if (bPreloadsChanged)
        {
            QMessageBox messageBox(this);
            messageBox.setText(tr("Preload requests have been modified.\n\nFor the new data to be loaded the audio system needs to be refreshed, this will stop all currently playing audio. Do you want to do this now?\n\nYou can always refresh manually at a later time through the Audio menu."));
            messageBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            messageBox.setDefaultButton(QMessageBox::No);
            messageBox.setWindowTitle("Audio Controls Editor");
            if (messageBox.exec() == QMessageBox::Yes)
            {
                QString sLevelName = GetIEditor()->GetLevelName();

                if (QString::compare(sLevelName, "Untitled", Qt::CaseInsensitive) == 0)
                {
                    // Rather pass empty QString to indicate that no level is loaded!
                    sLevelName = QString();
                }

                Audio::AudioSystemRequestBus::Broadcast(&Audio::AudioSystemRequestBus::Events::RefreshAudioSystem, sLevelName.toUtf8().data());
            }
        }
        m_pATLModel->ClearDirtyFlags();
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::UpdateInspector()
    {
        m_pInspectorPanel->SetSelectedControls(m_pATLControlsPanel->GetSelectedControls());
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::UpdateFilterFromSelection()
    {
        bool bAllSameType = true;
        EACEControlType selectedType = eACET_NUM_TYPES;
        ControlList ids = m_pATLControlsPanel->GetSelectedControls();
        size_t size = ids.size();
        for (size_t i = 0; i < size; ++i)
        {
            CATLControl* pControl = m_pATLModel->GetControlByID(ids[i]);
            if (pControl)
            {
                if (selectedType == eACET_NUM_TYPES)
                {
                    selectedType = pControl->GetType();
                }
                else if (selectedType != pControl->GetType())
                {
                    bAllSameType = false;
                }
            }
        }

        bool bSelectedFolder = (selectedType == eACET_NUM_TYPES);
        for (int i = 0; i < eACET_NUM_TYPES; ++i)
        {
            EACEControlType type = (EACEControlType)i;
            bool bAllowed = bSelectedFolder || (bAllSameType && selectedType == type);
            m_pAudioSystemPanel->SetAllowedControls((EACEControlType)i, bAllowed);
        }
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::UpdateAudioSystemData()
    {
        Audio::SAudioRequest oConfigDataRequest;
        oConfigDataRequest.nFlags = Audio::eARF_PRIORITY_HIGH;

        //clear the AudioSystem control config data
        Audio::SAudioManagerRequestData<Audio::eAMRT_CLEAR_CONTROLS_DATA> oClearRequestData(Audio::eADS_ALL);
        oConfigDataRequest.pData = &oClearRequestData;
        Audio::AudioSystemRequestBus::Broadcast(&Audio::AudioSystemRequestBus::Events::PushRequest, oConfigDataRequest);

        // parse the AudioSystem global config data
        // this is technically incorrect, we should just use GetControlsPath() unmodified when loading controls.
        // calling GetEditingGameDataFolder ensures that the reloaded file has been written to, a temp fix.
        // once we can listen to delete messages from Asset system, this can be changed to an EBus handler.
        const char* controlsPath = nullptr;
        Audio::AudioSystemRequestBus::BroadcastResult(controlsPath, &Audio::AudioSystemRequestBus::Events::GetControlsPath);
        AZStd::string sControlsPath(Path::GetEditingGameDataFolder());
        AzFramework::StringFunc::Path::Join(sControlsPath.c_str(), controlsPath, sControlsPath);
        Audio::SAudioManagerRequestData<Audio::eAMRT_PARSE_CONTROLS_DATA> oParseGlobalRequestData(sControlsPath.c_str(), Audio::eADS_GLOBAL);
        oConfigDataRequest.pData = &oParseGlobalRequestData;
        Audio::AudioSystemRequestBus::Broadcast(&Audio::AudioSystemRequestBus::Events::PushRequest, oConfigDataRequest);

        //parse the AudioSystem level-specific config data
        const char* levelName = GetIEditor()->GetLevelName().toUtf8().data();
        AzFramework::StringFunc::Path::Join(sControlsPath.c_str(), "levels", sControlsPath);
        AzFramework::StringFunc::Path::Join(sControlsPath.c_str(), levelName, sControlsPath);
        Audio::SAudioManagerRequestData<Audio::eAMRT_PARSE_CONTROLS_DATA> oParseLevelRequestData(sControlsPath.c_str(), Audio::eADS_LEVEL_SPECIFIC);
        oConfigDataRequest.pData = &oParseLevelRequestData;
        Audio::AudioSystemRequestBus::Broadcast(&Audio::AudioSystemRequestBus::Events::PushRequest, oConfigDataRequest);

        // inform the middleware specific plugin that the data has been saved
        // to disk (in case it needs to update something)
        CAudioControlsEditorPlugin::GetAudioSystemEditorImpl()->DataSaved();
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::OnEditorNotifyEvent(EEditorNotifyEvent event)
    {
        if (event == eNotify_OnEndSceneSave)
        {
            CAudioControlsEditorPlugin::ReloadScopes();
            m_pInspectorPanel->Reload();
        }
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::FilterControlType(EACEControlType type, bool bShow)
    {
        m_pAudioSystemPanel->SetAllowedControls(type, bShow);
    }

    //-------------------------------------------------------------------------------------------//
    void CAudioControlsEditorWindow::ReloadMiddlewareData()
    {
        IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetAudioSystemEditorImpl();
        if (pAudioSystemImpl)
        {
            pAudioSystemImpl->Reload();
        }
        m_pAudioSystemPanel->Reload();
        m_pInspectorPanel->Reload();
    }
} // namespace AudioControls

#include <Source/Editor/AudioControlsEditorWindow.moc>