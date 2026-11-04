// Couvre-LEDs de la TotoBox à bords droits, tenu par deux tenons (version découpée au laser).
// Coller sous l'en-tête 'FeatureScript ...; import(...)' d'un Feature Studio dédié à ce fichier.
//
// Le couvre-LEDs est une plaque inclinée posée de la cloison à la paroi droite, au-dessus des LEDs: son bord haut
// touche la façade, son bord bas s'arrête au-dessus de la carte. Modélisé avec deux biseaux (bord haut vertical
// contre la façade, bord bas horizontal), il demandait un ponçage. Cette Feature:
// - coupe ses deux bords d'équerre en gardant sa face du dessous (côté LEDs) telle quelle: la plaque ne perd que les
//   deux triangles des biseaux, et touche la façade par l'arête du dessous;
// - ajoute à chaque bout un tenon centré sur sa largeur, qui traverse la paroi voisine (cloison, paroi droite) dans
//   une mortaise inclinée comme la plaque: la plaque est posée à sa place, à son angle, sans gabarit.
// La plaque doit être une extrusion le long de X (bouts plans, perpendiculaires à X), ses deux grandes faces
// parallèles; les parois sont perpendiculaires à X, de part et d'autre. "Compensation de saignée" allonge le tenon
// et réduit la mortaise de la moitié de la valeur de chaque côté (comme les créneaux); à laisser à 0 si la
// compensation est faite dans le logiciel de découpe.

const COVER_TAB = { (millimeter) : [2, 10, 100] } as LengthBoundSpec;
const COVER_KERF = { (millimeter) : [0, 0, 2] } as LengthBoundSpec;

annotation { "Feature Type Name" : "Couvre-LEDs a bords droits TotoBox", "Feature Type Description" : "Coupe d'equerre les bords biseautes du couvre-LEDs et le tient par un tenon a chaque bout, dans une mortaise inclinee des parois voisines." }
export const totoboxLedCover = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Couvre-LEDs", "Filter" : EntityType.BODY && BodyType.SOLID, "MaxNumberOfPicks" : 1 }
        definition.cover is Query;
        annotation { "Name" : "Parois des tenons (cloison, paroi droite)", "Filter" : EntityType.BODY && BodyType.SOLID, "MaxNumberOfPicks" : 2 }
        definition.walls is Query;
        annotation { "Name" : "Longueur des tenons (le long de la plaque)" }
        isLength(definition.tabLength, COVER_TAB);
        annotation { "Name" : "Compensation de saignee" }
        isLength(definition.kerf, COVER_KERF);
    }
    {
        const mm = millimeter;
        const bounds = evBox3d(context, { "topology" : definition.cover, "tight" : true });
        const x0 = bounds.minCorner[0];
        const x1 = bounds.maxCorner[0];
        const y0 = bounds.minCorner[1];
        const z0 = bounds.minCorner[2];

        // Les deux grandes faces: ni perpendiculaires à X, ni horizontales, ni verticales.
        var upper;
        var lower;
        for (var face in evaluateQuery(context, qGeometry(qOwnedByBody(definition.cover, EntityType.FACE), GeometryType.PLANE)))
        {
            const p = evPlane(context, { "face" : face });
            if (abs(p.normal[0]) < 1e-6 && abs(p.normal[1]) > 0.1 && abs(p.normal[2]) > 0.1)
            {
                if (p.normal[2] > 0)
                    upper = p;
                else
                    lower = p;
            }
        }
        if (upper == undefined || lower == undefined)
        {
            throw regenError("Le couvre-LEDs doit etre une plaque inclinee extrudee le long de X.", ["cover"]);
        }
        const n = upper.normal;
        const t = abs(dot(upper.origin - lower.origin, n));

        // Section dans le plan YZ: [y, z]. zAt/yAt: intersection d'une face avec la verticale y ou l'horizontale z.
        const zAt = function(p, y) { return p.origin[2] - p.normal[1] * (y - p.origin[1]) / p.normal[2]; };
        const yAt = function(p, z) { return p.origin[1] - p.normal[2] * (z - p.origin[2]) / p.normal[1]; };
        const a = vector(y0, zAt(lower, y0)); // arête du dessous contre la façade
        const d = vector(yAt(lower, z0), z0); // arête du dessous en bas
        const up = vector(n[1], n[2]) * t;

        // Bords d'équerre: on retire les deux triangles des biseaux.
        const cuts = [[a, vector(y0, zAt(upper, y0)), a + up], [d, vector(yAt(upper, z0), z0), d + up]];
        var tools = [];
        for (var i = 0; i < size(cuts); i += 1)
        {
            if (norm(cuts[i][1] - cuts[i][2]) > 1e-4 * mm)
            {
                prism(context, id + ("bevel" ~ i), cuts[i], x0 - 1 * mm, x1 + 1 * mm);
                tools = append(tools, qCreatedBy(id + ("bevel" ~ i), EntityType.BODY));
            }
        }
        if (size(tools) > 0)
        {
            opBoolean(context, id + "square", { "tools" : qUnion(tools), "targets" : definition.cover, "operationType" : BooleanOperationType.SUBTRACTION });
        }

        // Tenons: section rectangulaire au milieu de la plaque, longueur le long de la plaque, épaisseur de la plaque.
        const along = (d - a) / norm(d - a);
        const across = up / norm(up);
        const center = (a + d) / 2 + up / 2;
        const section = function(halfLength, halfThickness) {
            return [center - along * halfLength - across * halfThickness, center + along * halfLength - across * halfThickness,
                    center + along * halfLength + across * halfThickness, center - along * halfLength + across * halfThickness];
        };
        const half = definition.kerf / 2;
        const walls = evaluateQuery(context, definition.walls);
        for (var i = 0; i < size(walls); i += 1)
        {
            const wall = evBox3d(context, { "topology" : walls[i], "tight" : true });
            var from;
            var to;
            if (wall.maxCorner[0] <= x0 + 0.5 * mm)
            {
                from = wall.minCorner[0];
                to = x0 + 0.5 * mm; // un peu dans la plaque, pour l'union (et l'écart de 0,01 mm avec la cloison)
            }
            else if (wall.minCorner[0] >= x1 - 0.5 * mm)
            {
                from = x1 - 0.5 * mm;
                to = wall.maxCorner[0];
            }
            else
            {
                throw regenError("Chaque paroi doit etre a un bout du couvre-LEDs (perpendiculaire a X).", ["walls"]);
            }
            prism(context, id + ("mortise" ~ i), section(definition.tabLength / 2 - half, t / 2 - half), from, to);
            opBoolean(context, id + ("cut" ~ i), { "tools" : qCreatedBy(id + ("mortise" ~ i), EntityType.BODY), "targets" : walls[i], "operationType" : BooleanOperationType.SUBTRACTION });
            prism(context, id + ("tab" ~ i), section(definition.tabLength / 2 + half, t / 2), from, to);
            opBoolean(context, id + ("join" ~ i), { "tools" : qUnion([definition.cover, qCreatedBy(id + ("tab" ~ i), EntityType.BODY)]), "operationType" : BooleanOperationType.UNION });
        }
    }, { "tabLength" : 10 * millimeter, "kerf" : 0 * millimeter });

// Prisme de section `points` ([y, z], plan YZ), de x = from à x = to.
function prism(context is Context, id is Id, points is array, from is ValueWithUnits, to is ValueWithUnits)
{
    const sketch = newSketchOnPlane(context, id + "sketch", { "sketchPlane" : plane(vector(from, 0 * millimeter, 0 * millimeter), vector(1, 0, 0), vector(0, 1, 0)) });
    skPolyline(sketch, "section", { "points" : append(points, points[0]) });
    skSolve(sketch);
    opExtrude(context, id + "extrude", { "entities" : qSketchRegion(id + "sketch"), "direction" : vector(1, 0, 0), "endBound" : BoundingType.BLIND, "endDepth" : to - from });
    opDeleteBodies(context, id + "deleteSketch", { "entities" : qCreatedBy(id + "sketch", EntityType.BODY) });
}
