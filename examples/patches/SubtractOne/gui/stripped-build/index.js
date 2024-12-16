export default function createView (patchConnection) {
    const iframe = document.createElement ("iframe");
    iframe.style.display = "block";
    iframe.style.border = "0";
    iframe.style.width = "100%";
    iframe.style.height = "100%";

    const base = new URL (".", import.meta.url);
    iframe.src = new URL ("./index.html", base);

    iframe.CmajorSingletonPatchConnection = patchConnection;

    return iframe;
}
