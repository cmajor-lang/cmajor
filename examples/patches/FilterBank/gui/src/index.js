import React from "react";
import { createRoot } from "react-dom/client";
import View from "./View";

(async () => {
  let CmajorSingletonPatchConnection = undefined;

  if (window.frameElement && window.frameElement.CmajorSingletonPatchConnection)
    CmajorSingletonPatchConnection =
      window.frameElement.CmajorSingletonPatchConnection;

  const container = document.getElementById("root");
  const root = createRoot(container);
  root.render(<View patchConnection={CmajorSingletonPatchConnection} />);
})();
