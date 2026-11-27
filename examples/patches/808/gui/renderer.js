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
//  This code may be used under either a GPLv3 or commercial
//  license: see LICENSE.md for more details.

/*
    A deliberately small WebGL2 renderer: just enough to draw lit, coloured or textured
    meshes, plus soft sprites for LED glows and shadows. It has no dependencies, so the
    patch works offline and inside any Cmajor host.
*/

//==============================================================================
// 4x4 matrices, stored column-major in Float32Arrays as WebGL expects
export const mat4 =
{
    identity()
    {
        return new Float32Array ([1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1]);
    },

    multiply (a, b)
    {
        const out = new Float32Array (16);

        for (let col = 0; col < 4; ++col)
            for (let row = 0; row < 4; ++row)
                out[col * 4 + row] = a[row]      * b[col * 4]
                                   + a[4 + row]  * b[col * 4 + 1]
                                   + a[8 + row]  * b[col * 4 + 2]
                                   + a[12 + row] * b[col * 4 + 3];
        return out;
    },

    translation (x, y, z)
    {
        const m = mat4.identity();
        m[12] = x; m[13] = y; m[14] = z;
        return m;
    },

    rotationY (angle)
    {
        const c = Math.cos (angle), s = Math.sin (angle);
        return new Float32Array ([c, 0, -s, 0,  0, 1, 0, 0,  s, 0, c, 0,  0, 0, 0, 1]);
    },

    perspective (fieldOfViewY, aspect, near, far)
    {
        const f = 1 / Math.tan (fieldOfViewY / 2), range = 1 / (near - far);
        return new Float32Array ([f / aspect, 0, 0, 0,  0, f, 0, 0,
                                  0, 0, (near + far) * range, -1,  0, 0, 2 * near * far * range, 0]);
    },

    lookAt (eye, target, up)
    {
        const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
        const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
        const norm = (v) => { const l = Math.hypot (...v); return v.map (x => x / l); };
        const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

        const z = norm (sub (eye, target)), x = norm (cross (up, z)), y = cross (z, x);

        return new Float32Array ([x[0], y[0], z[0], 0,  x[1], y[1], z[1], 0,  x[2], y[2], z[2], 0,
                                  -dot (x, eye), -dot (y, eye), -dot (z, eye), 1]);
    },

    invert (m)
    {
        const inv = new Float32Array (16);

        inv[0]  =  m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8]  =  m[4] * m[9]  * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5]  =  m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9]  = -m[0] * m[9]  * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] =  m[0] * m[9]  * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2]  =  m[1] * m[6]  * m[15] - m[1] * m[7]  * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]  - m[13] * m[3] * m[6];
        inv[6]  = -m[0] * m[6]  * m[15] + m[0] * m[7]  * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]  + m[12] * m[3] * m[6];
        inv[10] =  m[0] * m[5]  * m[15] - m[0] * m[7]  * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]  - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5]  * m[14] + m[0] * m[6]  * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]  + m[12] * m[2] * m[5];
        inv[3]  = -m[1] * m[6]  * m[11] + m[1] * m[7]  * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9]  * m[2] * m[7]  + m[9]  * m[3] * m[6];
        inv[7]  =  m[0] * m[6]  * m[11] - m[0] * m[7]  * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8]  * m[2] * m[7]  - m[8]  * m[3] * m[6];
        inv[11] = -m[0] * m[5]  * m[11] + m[0] * m[7]  * m[9]  + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]  - m[8]  * m[1] * m[7]  + m[8]  * m[3] * m[5];
        inv[15] =  m[0] * m[5]  * m[10] - m[0] * m[6]  * m[9]  - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]  + m[8]  * m[1] * m[6]  - m[8]  * m[2] * m[5];

        const det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        return inv.map (v => v / det);
    },

    transformPoint (m, [x, y, z])
    {
        const w = m[3] * x + m[7] * y + m[11] * z + m[15];
        return [(m[0] * x + m[4] * y + m[8]  * z + m[12]) / w,
                (m[1] * x + m[5] * y + m[9]  * z + m[13]) / w,
                (m[2] * x + m[6] * y + m[10] * z + m[14]) / w];
    }
};

/// Converts a "#rrggbb" colour to linear-light RGB, which is what the shaders work in
export function hexToLinear (hex)
{
    const value = parseInt (hex.slice (1), 16);
    return [value >> 16, (value >> 8) & 255, value & 255].map (c => Math.pow (c / 255, 2.2));
}

//==============================================================================
const surfaceVertexShader = `#version 300 es
in vec3 position;
in vec3 normal;
in vec2 uv;
uniform mat4 model, viewProjection;
out vec3 worldPosition, worldNormal;
out vec2 textureCoord;

void main()
{
    vec4 world = model * vec4 (position, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3 (model) * normal;
    textureCoord = uv;
    gl_Position = viewProjection * world;
}`;

const surfaceFragmentShader = `#version 300 es
precision highp float;
in vec3 worldPosition, worldNormal;
in vec2 textureCoord;
uniform vec3 colour, emissive, cameraPosition;
uniform float shininess, specular, useTexture;
uniform sampler2D surfaceTexture;
out vec4 fragColour;

// A studio setup: a big soft key light in front of the machine, a dimmer fill from the
// right, and a fake environment that reflects a bright ceiling and a dark floor.
// The key light is kept fairly low and well forward, because the camera is always in
// front of the machine: that way its mirror reflection in the flat panel points away
// from the camera, even when looking straight down at the panel, so the highlight
// only catches the curved edges of the knobs and keys rather than washing out the print.
const vec3 keyDirection  = normalize (vec3 (-0.3, 0.62, 0.72));
const vec3 fillDirection = normalize (vec3 (0.8, 0.45, 0.2));
const vec3 keyColour  = vec3 (1.0, 0.96, 0.9) * 2.0;
const vec3 fillColour = vec3 (0.75, 0.85, 1.0) * 0.5;

vec3 shadeLight (vec3 n, vec3 v, vec3 l, vec3 lightColour, vec3 albedo)
{
    float diffuse = max (dot (n, l), 0.0);
    vec3 h = normalize (l + v);
    float highlight = pow (max (dot (n, h), 0.0), shininess) * (shininess + 8.0) / 25.0;
    return lightColour * diffuse * (albedo + specular * highlight);
}

void main()
{
    vec3 albedo = colour;

    if (useTexture > 0.5)
        albedo = pow (texture (surfaceTexture, textureCoord).rgb, vec3 (2.2));

    vec3 n = normalize (worldNormal);
    vec3 v = normalize (cameraPosition - worldPosition);

    if (dot (n, v) < 0.0)
        n = -n;

    vec3 ambient = albedo * mix (vec3 (0.02, 0.02, 0.025), vec3 (0.16, 0.16, 0.18), n.y * 0.5 + 0.5);

    vec3 r = reflect (-v, n);
    vec3 environment = mix (vec3 (0.01), vec3 (0.9, 0.9, 0.92), smoothstep (0.1, 0.9, r.y))
                         + vec3 (0.25) * smoothstep (0.6, 0.95, r.z) * step (0.0, r.y);
    float fresnel = 0.04 + 0.96 * pow (1.0 - max (dot (n, v), 0.0), 5.0);

    vec3 lit = ambient
             + shadeLight (n, v, keyDirection, keyColour, albedo)
             + shadeLight (n, v, fillDirection, fillColour, albedo)
             + environment * fresnel * specular
             + emissive;

    // Filmic tone mapping (Narkowicz's ACES fit), then back to sRGB
    lit *= 0.8;
    lit = (lit * (2.51 * lit + 0.03)) / (lit * (2.43 * lit + 0.59) + 0.14);
    fragColour = vec4 (pow (clamp (lit, 0.0, 1.0), vec3 (1.0 / 2.2)), 1.0);
}`;

const spriteVertexShader = `#version 300 es
in vec3 position;
in vec2 uv;
uniform mat4 model, viewProjection;
out vec2 spriteCoord;

void main()
{
    spriteCoord = uv * 2.0 - 1.0;
    gl_Position = viewProjection * model * vec4 (position, 1.0);
}`;

const spriteFragmentShader = `#version 300 es
precision highp float;
in vec2 spriteCoord;
uniform vec3 colour;
uniform float intensity, softness, isShadow;
uniform vec2 aspect;
out vec4 fragColour;

void main()
{
    if (isShadow > 0.5)
    {
        // A soft-edged rounded rectangle, darkest in the middle
        vec2 d = max (abs (spriteCoord) * aspect - (aspect - vec2 (softness)), 0.0);
        float alpha = intensity * (1.0 - smoothstep (0.0, softness, length (d)));
        fragColour = vec4 (0.0, 0.0, 0.0, alpha);
    }
    else
    {
        // A gaussian blob, added on top of whatever is underneath
        float glow = intensity * exp (-dot (spriteCoord, spriteCoord) * 5.0);
        fragColour = vec4 (colour * glow, 0.0);
    }
}`;

//==============================================================================
export class Renderer
{
    constructor (canvas)
    {
        this.canvas = canvas;
        const gl = this.gl = canvas.getContext ("webgl2", { antialias: true, alpha: true, premultipliedAlpha: true });

        if (! gl)
            throw new Error ("WebGL2 is not available");

        this.surfaceProgram = this.createProgram (surfaceVertexShader, surfaceFragmentShader);
        this.spriteProgram  = this.createProgram (spriteVertexShader, spriteFragmentShader);
        this.anisotropy = gl.getExtension ("EXT_texture_filter_anisotropic");

        gl.enable (gl.DEPTH_TEST);
        gl.clearColor (0, 0, 0, 0);
    }

    createProgram (vertexSource, fragmentSource)
    {
        const gl = this.gl;

        const compile = (type, source) =>
        {
            const shader = gl.createShader (type);
            gl.shaderSource (shader, source);
            gl.compileShader (shader);

            if (! gl.getShaderParameter (shader, gl.COMPILE_STATUS))
                throw new Error (gl.getShaderInfoLog (shader));

            return shader;
        };

        const program = gl.createProgram();
        gl.attachShader (program, compile (gl.VERTEX_SHADER, vertexSource));
        gl.attachShader (program, compile (gl.FRAGMENT_SHADER, fragmentSource));
        gl.bindAttribLocation (program, 0, "position");
        gl.bindAttribLocation (program, 1, "normal");
        gl.bindAttribLocation (program, 2, "uv");
        gl.linkProgram (program);

        if (! gl.getProgramParameter (program, gl.LINK_STATUS))
            throw new Error (gl.getProgramInfoLog (program));

        const uniforms = {};
        const count = gl.getProgramParameter (program, gl.ACTIVE_UNIFORMS);

        for (let i = 0; i < count; ++i)
        {
            const name = gl.getActiveUniform (program, i).name;
            uniforms[name] = gl.getUniformLocation (program, name);
        }

        return { program, uniforms };
    }

    /// Uploads a mesh from geometry.js, returning a handle to pass to drawMesh()
    createMesh ({ positions, normals, uvs, indices })
    {
        const gl = this.gl;
        const vao = gl.createVertexArray();
        gl.bindVertexArray (vao);

        [positions, normals, uvs].forEach ((data, location) =>
        {
            gl.bindBuffer (gl.ARRAY_BUFFER, gl.createBuffer());
            gl.bufferData (gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
            gl.enableVertexAttribArray (location);
            gl.vertexAttribPointer (location, location === 2 ? 2 : 3, gl.FLOAT, false, 0, 0);
        });

        gl.bindBuffer (gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
        gl.bufferData (gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);
        gl.bindVertexArray (null);

        return { vao, count: indices.length };
    }

    /// Creates or updates a texture from a 2D canvas
    uploadTexture (source, existingTexture)
    {
        const gl = this.gl;
        const texture = existingTexture ?? gl.createTexture();

        gl.bindTexture (gl.TEXTURE_2D, texture);
        gl.texImage2D (gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, source);
        gl.generateMipmap (gl.TEXTURE_2D);
        gl.texParameteri (gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
        gl.texParameteri (gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri (gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri (gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

        if (this.anisotropy)
            gl.texParameterf (gl.TEXTURE_2D, this.anisotropy.TEXTURE_MAX_ANISOTROPY_EXT,
                              Math.min (8, gl.getParameter (this.anisotropy.MAX_TEXTURE_MAX_ANISOTROPY_EXT)));

        return texture;
    }

    //==============================================================================
    /// Resizes the drawing buffer to match the canvas's size on screen, and clears it
    beginFrame (viewProjection, cameraPosition)
    {
        const gl = this.gl, canvas = this.canvas;
        const scale = window.devicePixelRatio || 1;
        const width = Math.round (canvas.clientWidth * scale), height = Math.round (canvas.clientHeight * scale);

        if (canvas.width !== width || canvas.height !== height)
        {
            canvas.width = width;
            canvas.height = height;
        }

        gl.viewport (0, 0, width, height);
        gl.clear (gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

        this.viewProjection = viewProjection;
        this.cameraPosition = cameraPosition;
    }

    /// Draws a mesh using a material: { colour, specular, shininess, emissive, texture }
    drawMesh (mesh, model, material)
    {
        const gl = this.gl;
        const { program, uniforms } = this.surfaceProgram;

        gl.useProgram (program);
        gl.uniformMatrix4fv (uniforms.model, false, model);
        gl.uniformMatrix4fv (uniforms.viewProjection, false, this.viewProjection);
        gl.uniform3fv (uniforms.cameraPosition, this.cameraPosition);
        gl.uniform3fv (uniforms.colour, material.colour ?? [1, 1, 1]);
        gl.uniform3fv (uniforms.emissive, material.emissive ?? [0, 0, 0]);
        gl.uniform1f (uniforms.specular, material.specular ?? 0.5);
        gl.uniform1f (uniforms.shininess, material.shininess ?? 40);
        gl.uniform1f (uniforms.useTexture, material.texture ? 1 : 0);

        if (material.texture)
        {
            gl.activeTexture (gl.TEXTURE0);
            gl.bindTexture (gl.TEXTURE_2D, material.texture);
            gl.uniform1i (uniforms.surfaceTexture, 0);
        }

        gl.bindVertexArray (mesh.vao);
        gl.drawElements (gl.TRIANGLES, mesh.count, gl.UNSIGNED_INT, 0);
    }

    /// Draws a flat sprite in the x-z plane: either an additive glow or a soft shadow.
    /// The mesh should be a unit rectangle from createFlatRectangle (1, 1).
    drawSprite (unitRectangle, { x, y, z, width, depth, colour = [0, 0, 0], intensity = 1, isShadow = false, softness = 0.5 })
    {
        const gl = this.gl;
        const { program, uniforms } = this.spriteProgram;

        const model = mat4.multiply (mat4.translation (x - width / 2, y, z - depth / 2),
                                     new Float32Array ([width, 0, 0, 0,  0, 1, 0, 0,  0, 0, depth, 0,  0, 0, 0, 1]));

        gl.useProgram (program);
        gl.uniformMatrix4fv (uniforms.model, false, model);
        gl.uniformMatrix4fv (uniforms.viewProjection, false, this.viewProjection);
        gl.uniform3fv (uniforms.colour, colour);
        gl.uniform1f (uniforms.intensity, intensity);
        gl.uniform1f (uniforms.isShadow, isShadow ? 1 : 0);
        gl.uniform1f (uniforms.softness, softness);
        gl.uniform2f (uniforms.aspect, width / Math.max (width, depth), depth / Math.max (width, depth));

        gl.enable (gl.BLEND);
        gl.depthMask (false);

        if (isShadow)
            gl.blendFuncSeparate (gl.ZERO, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        else
            gl.blendFunc (gl.ONE, gl.ONE);

        gl.bindVertexArray (unitRectangle.vao);
        gl.drawElements (gl.TRIANGLES, unitRectangle.count, gl.UNSIGNED_INT, 0);

        gl.depthMask (true);
        gl.disable (gl.BLEND);
    }
}
