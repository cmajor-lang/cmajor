//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"


/// Returns a list of types of view that can be created for this patch
export function getAvailableViewTypes (patchConnection)
{
    if (! patchConnection)
        return [];

    if (patchConnection.manifest?.view?.src)
        return ["custom", "generic"];

    return ["generic"];
}

/// Creates and returns a HTMLElement view which can be shown to control this patch.
///
/// If no preferredType argument is supplied, this will return either a custom patch-specific
/// view (if the manifest specifies one), or a generic view if not. The preferredType argument
/// can be used to choose one of the types of view returned by getAvailableViewTypes().
export async function createPatchView (patchConnection, preferredType)
{
    if (patchConnection?.manifest)
    {
        let view = patchConnection.manifest.view;

        if (view && preferredType === "generic")
            if (view.src)
                view = undefined;

        const viewModuleURL = view?.src ? view.src : "/cmaj_api/cmaj-generic-patch-view.js";
        const viewModule = await import (patchConnection.getResourceAddress (viewModuleURL));
        const patchView = await viewModule?.default (patchConnection);

        if (patchView)
        {
            patchView.style.display = "block";

            if (view?.width > 10)
                patchView.style.width = view.width + "px";
            else
                patchView.style.width = undefined;

            if (view?.height > 10)
                patchView.style.height = view.height + "px";
            else
                patchView.style.height = undefined;

            return patchView;
        }
    }

    return undefined;
}
