//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

#pragma once

#include "cmaj_PatchPlayer.h"
#include "choc/gui/choc_DesktopWindow.h"
#include "../../../include/cmajor/helpers/cmaj_PatchWebView.h"

namespace cmaj
{

//==============================================================================
struct PatchWindow
{
    PatchWindow (const choc::value::Value& engineOptions,
                 const cmaj::BuildSettings& buildSettings)
        : player (engineOptions, buildSettings, true)
    {
        openGUITimer = choc::messageloop::Timer (50, [this]
        {
            if (openGUITimerRecursive || ! player.patch.isPlayable())
                return true;

            // On Windows, the openWindow call can run a synchronous message
            // loop which ends up in recursive timer callbacks!
            openGUITimerRecursive = true;
            openWindow();
            player.startPlayback();
            openGUITimerRecursive = false;
            return false;
        });

        player.onPatchLoaded = [this]
        {
            clearContent();
            updateWebViewWithDefaultPatchView();
            setWebViewActive (true);
            refreshWindow();
        };

        player.onPatchUnloaded = [this]
        {
            clearContent();
            setWebViewActive (false);
        };
    }

    ~PatchWindow()
    {
        closeWindow();
    }

    void openWindow()
    {
        closeWindow();

        window = std::make_unique<choc::ui::DesktopWindow> (choc::ui::Bounds { 100, 100, 650, 650 });
        window->setWindowTitle (player.patch.getName());
        window->windowClosed = [this] { windowClosed(); };
        window->setBounds ({ 100, 100, 600, 500 });
        window->setResizable (true);
        window->setMinimumSize (200, 200);
        window->setVisible (true);
        window->toFront();

        shouldPositionWindow = true;
        createView();
    }

    void closeWindow()
    {
        removeView();
        window.reset();
    }

    void windowClosed()
    {
        player.setAudioMIDIPlayer ({});
        choc::messageloop::stop();
    }

    cmaj::PatchPlayer player;

private:
    std::unique_ptr<choc::ui::DesktopWindow> window;
    std::unique_ptr<cmaj::PatchWebView> view;
    choc::messageloop::Timer openGUITimer;
    bool openGUITimerRecursive = false;
    bool shouldPositionWindow = true;

    void createView()
    {
        if (window != nullptr && player.patch.isPlayable() && view == nullptr)
        {
            view = cmaj::PatchWebView::create (player.patch, extractView (player.patch));
            refreshWindow();
        }
    }

    static PatchManifest::View extractView (const Patch& patch)
    {
        if (auto manifest = patch.getManifest())
            if (auto* maybeView = manifest->findDefaultView())
                return *maybeView;

        return {};
    }

    void refreshWindow()
    {
        if (window != nullptr && player.patch.isPlayable() && view != nullptr)
        {
            window->setWindowTitle (player.patch.getName());
            window->setContent (view->getWebView().getViewHandle());

            if (shouldPositionWindow)
            {
                shouldPositionWindow = false;
                window->setBounds ({ 100, 100, (int) view->width, (int) view->height });
            }

            window->setResizable (view->resizable);
        }
    }

    void clearContent()
    {
        if (window != nullptr)
            window->setContent (nullptr);
    }

    void updateWebViewWithDefaultPatchView()
    {
        if (player.patch.isPlayable() && view != nullptr)
            view->update (extractView (player.patch));
    }

    void setWebViewActive (bool active)
    {
        if (view != nullptr)
        {
            view->setActive (active);
            view->reload();
        }
    }

    void removeView()
    {
        clearContent();
        view.reset();
    }
};

} // namespace cmaj
