const path = require("path");
const native = require(path.join(__dirname, "..", "build", "Release", "cmaj-node-bindings.node"));
module.exports = native;
Object.assign(exports, native);