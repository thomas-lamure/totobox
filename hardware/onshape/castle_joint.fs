// Assemblage à créneaux ("castle joint", "finger joint") entre deux plaques de la TotoBox découpées au laser.
// Coller sous l'en-tête 'FeatureScript ...; import(...)' d'un Feature Studio dédié à ce fichier.
//
// Les deux plaques sont parallèles aux plans du repère et se rejoignent à angle droit: en coin (comme deux parois),
// en bord (dessus posé sur une paroi) ou en T (cloison contre une paroi: tenons dans des mortaises). La zone de
// jonction est le volume commun aux deux épaisseurs: épaisseur de A x épaisseur de B, sur la longueur commune des
// deux plaques le long de l'arête. Elle est découpée en un nombre impair de créneaux d'environ "Largeur visée",
// attribués alternativement à A (créneaux des deux bouts) puis à B: chaque créneau est ajouté à sa plaque et retiré
// de l'autre. Un petit écart (0,01 mm) entre les plaques est comblé. "Compensation de saignée" élargit les créneaux
// de chaque plaque de la moitié de la valeur de chaque côté (serrage au laser); à laisser à 0 si la compensation est
// faite dans le logiciel de découpe.
//
// "Limiter la longueur de l'arête": la jonction ne porte que sur [Début, Fin] le long de l'arête (coordonnées du
// Part Studio). Indispensable aux coins où trois plaques se rencontrent (dessus et deux parois): sans limite, les
// tenons déjà ajoutés à une plaque allongent l'arête suivante jusque dans un coin qui appartient à une autre
// jonction, et les plaques se chevauchent.

const JOINT_FINGER = { (millimeter) : [1, 10, 200] } as LengthBoundSpec;
const JOINT_KERF = { (millimeter) : [0, 0, 2] } as LengthBoundSpec;
const JOINT_POSITION = { (millimeter) : [-10000, 0, 10000] } as LengthBoundSpec;

annotation { "Feature Type Name" : "Assemblage a creneaux TotoBox", "Feature Type Description" : "Creneaux alternes entre deux plaques a angle droit (coin, bord ou T): nombre impair de creneaux, ceux des bouts a la plaque A." }
export const totoboxCastleJoint = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Plaque A (creneaux des bouts)", "Filter" : EntityType.BODY && BodyType.SOLID, "MaxNumberOfPicks" : 1 }
        definition.partA is Query;
        annotation { "Name" : "Plaque B", "Filter" : EntityType.BODY && BodyType.SOLID, "MaxNumberOfPicks" : 1 }
        definition.partB is Query;
        annotation { "Name" : "Largeur visee des creneaux" }
        isLength(definition.fingerWidth, JOINT_FINGER);
        annotation { "Name" : "Compensation de saignee" }
        isLength(definition.kerf, JOINT_KERF);
        annotation { "Name" : "Limiter la longueur de l'arete" }
        definition.limitEdge is boolean;
        if (definition.limitEdge)
        {
            annotation { "Name" : "Debut (coordonnee le long de l'arete)" }
            isLength(definition.limitStart, JOINT_POSITION);
            annotation { "Name" : "Fin" }
            isLength(definition.limitEnd, JOINT_POSITION);
        }
    }
    {
        if (isQueryEmpty(context, definition.partA) || isQueryEmpty(context, definition.partB))
        {
            throw regenError("Choisir les deux plaques.", ["partA", "partB"]);
        }
        const boxA = evBox3d(context, { "topology" : definition.partA, "tight" : true });
        const boxB = evBox3d(context, { "topology" : definition.partB, "tight" : true });
        const thinA = jointThinAxis(boxA);
        const thinB = jointThinAxis(boxB);
        if (thinA == thinB)
        {
            throw regenError("Les deux plaques sont paralleles: pas d'assemblage a angle droit.");
        }
        const edgeAxis = 3 - thinA - thinB;

        // Zone de jonction: épaisseur de A (prolongée jusqu'à B s'il reste un petit écart), épaisseur de B (idem
        // vers A), longueur commune le long de l'arête.
        const rangeA = jointBridge(boxA.minCorner[thinA], boxA.maxCorner[thinA], boxB.minCorner[thinA], boxB.maxCorner[thinA]);
        const rangeB = jointBridge(boxB.minCorner[thinB], boxB.maxCorner[thinB], boxA.minCorner[thinB], boxA.maxCorner[thinB]);
        var edgeStart = max(boxA.minCorner[edgeAxis], boxB.minCorner[edgeAxis]);
        var edgeEnd = min(boxA.maxCorner[edgeAxis], boxB.maxCorner[edgeAxis]);
        if (definition.limitEdge)
        {
            edgeStart = max(edgeStart, min(definition.limitStart, definition.limitEnd));
            edgeEnd = min(edgeEnd, max(definition.limitStart, definition.limitEnd));
        }
        const length = edgeEnd - edgeStart;
        if (length <= 0 * millimeter)
        {
            throw regenError("Les plaques ne se rejoignent pas le long d'une arete.");
        }

        // Nombre impair de créneaux, au moins 3, au plus proche de la largeur visée
        var count = round(length / definition.fingerWidth);
        if (count % 2 == 0)
        {
            count += (length / definition.fingerWidth > count) ? 1 : -1;
        }
        count = max(3, count);
        const step = length / count;
        const half = definition.kerf / 2;

        for (var i = 0; i < count; i += 1)
        {
            const ownerIsA = i % 2 == 0;
            const owner = ownerIsA ? definition.partA : definition.partB;
            const other = ownerIsA ? definition.partB : definition.partA;
            const start = edgeStart + step * i;
            const end = start + step;
            // Créneau ajouté à sa plaque (élargi de la compensation, sans dépasser l'arête) et retiré de l'autre
            const addStart = max(edgeStart, start - half);
            const addEnd = min(edgeEnd, end + half);
            fCuboid(context, id + ("add" ~ i), { "corner1" : jointPoint(thinA, rangeA[0], thinB, rangeB[0], edgeAxis, addStart), "corner2" : jointPoint(thinA, rangeA[1], thinB, rangeB[1], edgeAxis, addEnd) });
            // Union: toutes les pièces dans "tools", la plaque en premier pour qu'elle garde son nom
            opBoolean(context, id + ("union" ~ i), { "tools" : qUnion([owner, qCreatedBy(id + ("add" ~ i), EntityType.BODY)]), "operationType" : BooleanOperationType.UNION });
            fCuboid(context, id + ("cut" ~ i), { "corner1" : jointPoint(thinA, rangeA[0], thinB, rangeB[0], edgeAxis, start), "corner2" : jointPoint(thinA, rangeA[1], thinB, rangeB[1], edgeAxis, end) });
            opBoolean(context, id + ("subtract" ~ i), { "tools" : qCreatedBy(id + ("cut" ~ i), EntityType.BODY), "targets" : other, "operationType" : BooleanOperationType.SUBTRACTION });
        }
    }, { "fingerWidth" : 10 * millimeter, "kerf" : 0 * millimeter, "limitEdge" : false, "limitStart" : 0 * millimeter, "limitEnd" : 0 * millimeter });

// Axe de la plus petite dimension (épaisseur de la plaque): 0 = X, 1 = Y, 2 = Z.
function jointThinAxis(bounds is Box3d) returns number
{
    const size = bounds.maxCorner - bounds.minCorner;
    if (size[0] <= size[1] && size[0] <= size[2])
        return 0;
    if (size[1] <= size[2])
        return 1;
    return 2;
}

// Intervalle [lo, hi] d'une épaisseur, prolongé jusqu'à l'autre plaque [otherLo, otherHi] si elle commence juste
// après ou finit juste avant (écart de modélisation).
function jointBridge(lo is ValueWithUnits, hi is ValueWithUnits, otherLo is ValueWithUnits, otherHi is ValueWithUnits) returns array
{
    var start = lo;
    var end = hi;
    if (otherLo > hi)
        end = otherLo;
    if (otherHi < lo)
        start = otherHi;
    return [start, end];
}

function jointPoint(axisA is number, valueA is ValueWithUnits, axisB is number, valueB is ValueWithUnits, axisEdge is number, valueEdge is ValueWithUnits) returns Vector
{
    var point = [0 * millimeter, 0 * millimeter, 0 * millimeter];
    point[axisA] = valueA;
    point[axisB] = valueB;
    point[axisEdge] = valueEdge;
    return vector(point);
}
