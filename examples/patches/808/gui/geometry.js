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
    Functions that build the triangle meshes for the machine. Each returns an object
    with flat arrays of positions, normals and texture coordinates, plus triangle indices,
    ready to be uploaded by the renderer.
*/

class MeshBuilder
{
    positions = [];
    normals = [];
    uvs = [];
    indices = [];

    addVertex (position, normal, uv = [0, 0])
    {
        this.positions.push (...position);
        this.normals.push (...normal);
        this.uvs.push (...uv);
        return this.positions.length / 3 - 1;
    }

    addQuad (a, b, c, d)
    {
        this.indices.push (a, b, c, a, c, d);
    }

    getMesh()
    {
        return { positions: new Float32Array (this.positions),
                 normals:   new Float32Array (this.normals),
                 uvs:       new Float32Array (this.uvs),
                 indices:   new Uint32Array (this.indices) };
    }
}

function normalise ([x, y, z])
{
    const length = Math.hypot (x, y, z) || 1;
    return [x / length, y / length, z / length];
}

//==============================================================================
/**
    A box with all its edges and corners rounded off, sitting with its base at y = 0
    and centred on the origin in x and z.

    It's made by taking a subdivided cube and pulling each vertex onto the surface of a
    smaller "inner" box inflated by the corner radius, which also gives us the normals.
*/
export function createRoundedBox (width, height, depth, radius, cornerSegments = 4)
{
    const mesh = new MeshBuilder();
    const half = [width / 2, height / 2, depth / 2];
    const inner = half.map (h => Math.max (0, h - radius));

    // The grid coordinates along each axis, bunched up around the curved corners
    const getAxisCoordinates = (axis) =>
    {
        const coords = [];

        for (let i = 0; i <= cornerSegments; ++i)
            coords.push (-inner[axis] - radius * Math.cos ((i / cornerSegments) * Math.PI / 2));

        for (let i = 0; i <= cornerSegments; ++i)
            coords.push (inner[axis] + radius * Math.sin ((i / cornerSegments) * Math.PI / 2));

        return coords;
    };

    const axisCoords = [0, 1, 2].map (getAxisCoordinates);

    // For each face: the axis it faces along, its sign, and the two axes it spans
    const faces = [[0, 1, 1, 2], [0, -1, 2, 1], [1, 1, 2, 0], [1, -1, 0, 2], [2, 1, 0, 1], [2, -1, 1, 0]];

    for (const [axis, sign, uAxis, vAxis] of faces)
    {
        const us = axisCoords[uAxis], vs = axisCoords[vAxis];
        const first = mesh.positions.length / 3;

        for (const v of vs)
        {
            for (const u of us)
            {
                const p = [0, 0, 0];
                p[axis] = sign * half[axis];
                p[uAxis] = u;
                p[vAxis] = v;

                const clamped = p.map ((value, i) => Math.max (-inner[i], Math.min (inner[i], value)));
                let normal = normalise (p.map ((value, i) => value - clamped[i]));

                if (Math.hypot (...p.map ((value, i) => value - clamped[i])) < 1e-6)
                {
                    normal = [0, 0, 0];
                    normal[axis] = sign;
                }

                const position = clamped.map ((value, i) => value + normal[i] * radius);
                position[1] += half[1];

                mesh.addVertex (position, normal);
            }
        }

        const rowLength = us.length;

        for (let j = 0; j < vs.length - 1; ++j)
            for (let i = 0; i < rowLength - 1; ++i)
                mesh.addQuad (first + j * rowLength + i,
                              first + (j + 1) * rowLength + i,
                              first + (j + 1) * rowLength + i + 1,
                              first + j * rowLength + i + 1);
    }

    return mesh.getMesh();
}

//==============================================================================
/**
    Spins a 2D profile around the y axis. The profile is a list of [radius, y] points
    running from the outside bottom edge up and over towards the centre of the top.

    Each segment of the profile gets its own normals, so the corners between segments
    stay crisp, like machined edges. If knurlCount is non-zero, every point at knurlRadius
    is given that many ridges around its circumference, like the grip of a knob.
*/
export function createLathe (profile, { radialSegments = 64, knurlCount = 0, knurlDepth = 0, knurlRadius = -1 } = {})
{
    const mesh = new MeshBuilder();

    for (let p = 0; p < profile.length - 1; ++p)
    {
        const [r0, y0] = profile[p], [r1, y1] = profile[p + 1];
        const [profileNormalR, profileNormalY] = normalise ([y1 - y0, r0 - r1, 0]);
        const isKnurled0 = knurlCount > 0 && r0 === knurlRadius;
        const isKnurled1 = knurlCount > 0 && r1 === knurlRadius;
        const first = mesh.positions.length / 3;

        for (let i = 0; i <= radialSegments; ++i)
        {
            const angle = (i / radialSegments) * Math.PI * 2;
            const radialX = Math.sin (angle), radialZ = Math.cos (angle);

            // r(a) = r0 * (1 + depth * cos (n * a)), so the normal tilts along the tangent by -r'(a)
            const scale = 1 + knurlDepth * Math.cos (knurlCount * angle);
            const scale0 = isKnurled0 ? scale : 1, scale1 = isKnurled1 ? scale : 1;
            let normal = [radialX * profileNormalR, profileNormalY, radialZ * profileNormalR];

            if (isKnurled0 && isKnurled1)
            {
                const slope = -knurlDepth * knurlCount * Math.sin (knurlCount * angle);
                normal = normalise ([radialX * scale - radialZ * slope, 0, radialZ * scale + radialX * slope]);
            }

            mesh.addVertex ([radialX * r0 * scale0, y0, radialZ * r0 * scale0], normal);
            mesh.addVertex ([radialX * r1 * scale1, y1, radialZ * r1 * scale1], normal);
        }

        for (let i = 0; i < radialSegments; ++i)
        {
            const a = first + i * 2;
            mesh.addQuad (a, a + 2, a + 3, a + 1);
        }
    }

    return mesh.getMesh();
}

//==============================================================================
/// A flat rectangle in the x-z plane, facing upwards, with texture coordinates 0 to 1
export function createFlatRectangle (width, depth)
{
    const mesh = new MeshBuilder();
    const up = [0, 1, 0];

    mesh.addVertex ([0, 0, 0],         up, [0, 0]);
    mesh.addVertex ([0, 0, depth],     up, [0, 1]);
    mesh.addVertex ([width, 0, depth], up, [1, 1]);
    mesh.addVertex ([width, 0, 0],     up, [1, 0]);
    mesh.addQuad (0, 1, 2, 3);

    return mesh.getMesh();
}

//==============================================================================
/// The three parts of a knob: the skirt and knurled body, the coloured cap, and the pointer line
export function createKnobMeshes ({ radius, skirtRadius, height })
{
    const capRadius = radius - 1.3;
    const skirtHeight = 2.2;

    const body = createLathe ([[skirtRadius, 0],
                               [skirtRadius, skirtHeight - 0.6],
                               [skirtRadius - 0.6, skirtHeight],
                               [radius + 0.2, skirtHeight + 0.3],
                               [radius, skirtHeight + 0.3],
                               [radius, height - 1.4],
                               [radius - 0.5, height - 0.4],
                               [capRadius + 0.3, height],
                               [capRadius, height]],
                              { knurlCount: Math.round (radius * 2.6), knurlDepth: 0.025, knurlRadius: radius });

    const cap = createLathe ([[capRadius, height - 0.1],
                              [capRadius - 0.4, height + 0.25],
                              [capRadius * 0.6, height + 0.45],
                              [0, height + 0.5]]);

    const pointerLength = capRadius * 0.85;
    const pointer = createRoundedBox (radius * 0.14, 0.3, pointerLength, 0.1, 1);
    pointer.positions.forEach ((_, i, p) =>
    {
        if (i % 3 === 1) p[i] += height + 0.42;
        if (i % 3 === 2) p[i] -= pointerLength / 2 + capRadius * 0.08;
    });

    return { body, cap, pointer };
}

/// A small domed LED lens sitting in a dark bezel ring
export function createLEDMeshes (radius)
{
    const bezel = createLathe ([[radius + 1.2, 0], [radius + 1.2, 0.5], [radius + 0.9, 0.9], [radius, 0.9]], { radialSegments: 24 });

    const dome = [];
    for (let i = 0; i <= 6; ++i)
    {
        const a = (i / 6) * Math.PI / 2;
        dome.push ([radius * Math.cos (a), 0.4 + radius * 0.55 * Math.sin (a)]);
    }

    return { bezel, lens: createLathe (dome, { radialSegments: 24 }) };
}
