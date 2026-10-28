// Haut-parleur rectangulaire de la TotoBox: modèle simplifié pour dessiner le boîtier.
// Cotes par défaut mesurées (35 x 25 mm, 6,3 mm d'épaisseur totale dont 0,5 mm de joint), réglables dans la Feature.
// Coller sous l'en-tête 'FeatureScript ...; import(...)' d'un Feature Studio dédié à ce fichier.
//
// Repère: origine au centre du haut-parleur, Z = 0 au dos du corps, face avant (membrane et joint adhésif)
// vers +Z, fils sortant par le petit côté +X.

const SPK_LENGTH = { (millimeter) : [15, 35, 150] } as LengthBoundSpec;
const SPK_WIDTH = { (millimeter) : [10, 25, 150] } as LengthBoundSpec;
const SPK_THICKNESS = { (millimeter) : [1.5, 6.3, 50] } as LengthBoundSpec;
const SPK_GASKET = { (millimeter) : [0.1, 0.5, 5] } as LengthBoundSpec;
const SPK_BORDER = { (millimeter) : [0.5, 2.2, 10] } as LengthBoundSpec;
const SPK_WIRE = { (millimeter) : [1, 20, 300] } as LengthBoundSpec;

annotation { "Feature Type Name" : "Haut-parleur TotoBox", "Feature Type Description" : "Haut-parleur rectangulaire simplifie. Origine au centre, Z = 0 au dos, face avant vers +Z, fils vers +X." }
export const totoboxSpeaker = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Longueur hors tout" }
        isLength(definition.length, SPK_LENGTH);
        annotation { "Name" : "Largeur hors tout" }
        isLength(definition.width, SPK_WIDTH);
        annotation { "Name" : "Epaisseur totale (avec joint)" }
        isLength(definition.thickness, SPK_THICKNESS);
        annotation { "Name" : "Epaisseur du joint adhesif" }
        isLength(definition.gasketThickness, SPK_GASKET);
        annotation { "Name" : "Largeur du bord du joint" }
        isLength(definition.gasketBorder, SPK_BORDER);
        annotation { "Name" : "Longueur des fils hors du haut-parleur" }
        isLength(definition.wireLength, SPK_WIRE);
    }
    {
        const mm = millimeter;
        const L = definition.length;
        const W = definition.width;
        // Profondeur du corps = épaisseur totale mesurée - joint adhésif
        const front = definition.thickness;
        const D = front - definition.gasketThickness;
        if (D <= 0 * mm)
        {
            throw regenError("L'epaisseur totale doit depasser celle du joint adhesif.");
        }
        const b = definition.gasketBorder;

        // Corps (saladier et aimant), coins arrondis
        fCuboid(context, id + "body", { "corner1" : vector(-L / 2, -W / 2, 0 * mm), "corner2" : vector(L / 2, W / 2, D) });
        opFillet(context, id + "bodyCorners", { "entities" : qParallelEdges(qCreatedBy(id + "body", EntityType.EDGE), Z_DIRECTION), "radius" : W * 0.13 });
        spkDressPart(context, qCreatedBy(id + "body", EntityType.BODY), "Haut-parleur (corps)", color(0.1, 0.1, 0.11));

        // Membrane visible dans l'ouverture du joint
        fCuboid(context, id + "cone", { "corner1" : vector(-L * 0.35, -W * 0.37, D), "corner2" : vector(L * 0.35, W * 0.37, D + 0.2 * mm) });
        opFillet(context, id + "coneCorners", { "entities" : qParallelEdges(qCreatedBy(id + "cone", EntityType.EDGE), Z_DIRECTION), "radius" : W * 0.3 });
        spkDressPart(context, qCreatedBy(id + "cone", EntityType.BODY), "Haut-parleur (membrane)", color(0.22, 0.22, 0.24));

        // Joint adhésif en cadre sur la face avant: c'est lui qui se colle contre la paroi du boîtier
        fCuboid(context, id + "gasket", { "corner1" : vector(-L / 2, -W / 2, D), "corner2" : vector(L / 2, W / 2, front) });
        opFillet(context, id + "gasketCorners", { "entities" : qParallelEdges(qCreatedBy(id + "gasket", EntityType.EDGE), Z_DIRECTION), "radius" : W * 0.15 });
        fCuboid(context, id + "opening", { "corner1" : vector(-(L / 2 - b), -(W / 2 - b), D - 1 * mm), "corner2" : vector(L / 2 - b, W / 2 - b, front + 1 * mm) });
        opFillet(context, id + "openingCorners", { "entities" : qParallelEdges(qCreatedBy(id + "opening", EntityType.EDGE), Z_DIRECTION), "radius" : (W / 2 - b) * 0.65 });
        opBoolean(context, id + "openingCut", { "tools" : qCreatedBy(id + "opening", EntityType.BODY), "targets" : qCreatedBy(id + "gasket", EntityType.BODY), "operationType" : BooleanOperationType.SUBTRACTION });
        spkDressPart(context, qCreatedBy(id + "gasket", EntityType.BODY), "Haut-parleur (joint adhesif)", color(0.95, 0.95, 0.95));

        // Fils vers le connecteur SPK de la carte
        const wireY = -W * 0.1;
        fCylinder(context, id + "wireBlack", { "bottomCenter" : vector(L / 2 - 2 * mm, wireY - 0.7 * mm, D / 2), "topCenter" : vector(L / 2 + definition.wireLength, wireY - 0.7 * mm, D / 2), "radius" : 0.6 * mm });
        fCylinder(context, id + "wireRed", { "bottomCenter" : vector(L / 2 - 2 * mm, wireY + 0.7 * mm, D / 2), "topCenter" : vector(L / 2 + definition.wireLength, wireY + 0.7 * mm, D / 2), "radius" : 0.6 * mm });
        spkDressPart(context, qCreatedBy(id + "wireBlack", EntityType.BODY), "Haut-parleur (fil noir)", color(0.1, 0.1, 0.1));
        spkDressPart(context, qCreatedBy(id + "wireRed", EntityType.BODY), "Haut-parleur (fil rouge)", color(0.85, 0.1, 0.1));
    });

function spkDressPart(context is Context, part is Query, name is string, appearance is Color)
{
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.NAME, "value" : name });
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.APPEARANCE, "value" : appearance });
}
