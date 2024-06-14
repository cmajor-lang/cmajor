class PatchView extends HTMLElement {
  constructor(patchConnection) {
    super();
    this.attachShadow({ mode: "open" });
    this.patchConnection = patchConnection;
  }

  connectedCallback() {
    const iframe = document.createElement("iframe");
    iframe.style.display = "block";
    iframe.style.border = "0";
    iframe.style.width = "100%";
    iframe.style.height = "100%";

    const base = new URL(".", import.meta.url);
    iframe.src = new URL("./index.html", base);

    iframe.CmajorSingletonPatchConnection = this.patchConnection;
    this.shadowRoot.appendChild(iframe);
  }

  getScaleFactorLimits() {
    return { minScale: 0.5, maxScale: 1.25 };
  }
}

export default function createPatchView(patchConnection) {
  return new PatchView(patchConnection);
}

customElements.define("custom-patch-view", PatchView);
