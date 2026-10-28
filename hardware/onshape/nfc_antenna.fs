// Module d'antenne NFC HermitX: modèle simplifié pour dessiner le boîtier TotoBox.
// Cotes par défaut mesurées sur le module (25 x 38 x 2 mm dont 0,5 mm d'adhésif, fils de 1 mm soudés à 9,5 mm du bord),
// réglables dans la boîte de dialogue de la Feature.
// Coller sous l'en-tête 'FeatureScript ...; import(...)' d'un Feature Studio dédié à ce fichier.
//
// Repère: origine au coin inférieur gauche du module, côté sortie des fils en Y = 0 (les fils partent vers -Y),
// Z = 0 sous l'adhésif double face, face imprimée (logo, soudures) vers +Z.

const NFC_LENGTH = { (millimeter) : [10, 38, 150] } as LengthBoundSpec;
const NFC_WIDTH = { (millimeter) : [10, 25, 150] } as LengthBoundSpec;
const NFC_PAD = { (millimeter) : [1, 9.5, 150] } as LengthBoundSpec;
const NFC_WIRE_D = { (millimeter) : [0.3, 1, 5] } as LengthBoundSpec;
const NFC_PCB = { (millimeter) : [0.4, 1.5, 3] } as LengthBoundSpec;
const NFC_TAPE = { (millimeter) : [0.1, 0.5, 5] } as LengthBoundSpec;
const NFC_WIRE = { (millimeter) : [1, 20, 300] } as LengthBoundSpec;

annotation { "Feature Type Name" : "Antenne NFC HermitX", "Feature Type Description" : "Module d'antenne NFC HermitX simplifie. Origine au coin inferieur gauche, Z = 0 sous l'adhesif, fils vers -Y." }
export const hermitxNfcAntenna = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Longueur (sens des fils)" }
        isLength(definition.length, NFC_LENGTH);
        annotation { "Name" : "Largeur" }
        isLength(definition.width, NFC_WIDTH);
        annotation { "Name" : "Epaisseur du PCB" }
        isLength(definition.pcbThickness, NFC_PCB);
        annotation { "Name" : "Epaisseur de l'adhesif" }
        isLength(definition.tapeThickness, NFC_TAPE);
        annotation { "Name" : "Distance des soudures au bord" }
        isLength(definition.padDistance, NFC_PAD);
        annotation { "Name" : "Diametre des fils" }
        isLength(definition.wireDiameter, NFC_WIRE_D);
        annotation { "Name" : "Longueur des fils hors du module" }
        isLength(definition.wireLength, NFC_WIRE);
    }
    {
        const mm = millimeter;
        const L = definition.length;
        const W = definition.width;
        const tape = definition.tapeThickness;
        const top = tape + definition.pcbThickness;

        // Adhésif double face sous le module
        fCuboid(context, id + "tape", { "corner1" : vector(0 * mm, 0 * mm, 0 * mm), "corner2" : vector(W, L, tape) });
        nfcDressPart(context, qCreatedBy(id + "tape", EntityType.BODY), "Antenne NFC (adhesif)", color(0.95, 0.95, 0.95));

        // PCB d'antenne, coins arrondis, petit trou au-dessus du logo
        fCuboid(context, id + "pcb", { "corner1" : vector(0 * mm, 0 * mm, tape), "corner2" : vector(W, L, top) });
        opFillet(context, id + "pcbCorners", { "entities" : qParallelEdges(qCreatedBy(id + "pcb", EntityType.EDGE), Z_DIRECTION), "radius" : 1.5 * mm });
        fCylinder(context, id + "hole", { "bottomCenter" : vector(W / 2, L * 0.6, tape / 2), "topCenter" : vector(W / 2, L * 0.6, top + 1 * mm), "radius" : 1 * mm });
        opBoolean(context, id + "holeCut", { "tools" : qCreatedBy(id + "hole", EntityType.BODY), "targets" : qCreatedBy(id + "pcb", EntityType.BODY), "operationType" : BooleanOperationType.SUBTRACTION });
        nfcDressPart(context, qCreatedBy(id + "pcb", EntityType.BODY), "Antenne NFC (PCB)", color(0.07, 0.07, 0.08));

        // Soudures des deux fils, côté sortie; les fils sont posés à plat sur le PCB
        const padY = definition.padDistance;
        const wireR = definition.wireDiameter / 2;
        const wireZ = top + wireR;
        fCylinder(context, id + "solder1", { "bottomCenter" : vector(W / 2 - 1 * mm, padY, top), "topCenter" : vector(W / 2 - 1 * mm, padY, top + 2 * wireR), "radius" : 0.75 * mm });
        fCylinder(context, id + "solder2", { "bottomCenter" : vector(W / 2 + 1 * mm, padY, top), "topCenter" : vector(W / 2 + 1 * mm, padY, top + 2 * wireR), "radius" : 0.75 * mm });
        nfcDressPart(context, qCreatedBy(id + "solder1", EntityType.BODY), "Antenne NFC (soudure noir)", color(0.8, 0.8, 0.8));
        nfcDressPart(context, qCreatedBy(id + "solder2", EntityType.BODY), "Antenne NFC (soudure rouge)", color(0.8, 0.8, 0.8));

        // Bande Kapton qui plaque les fils, entre les soudures et le bord
        fCuboid(context, id + "kapton", { "corner1" : vector(W * 0.1, padY * 0.25, wireZ + wireR), "corner2" : vector(W * 0.75, padY * 0.7, wireZ + wireR + 0.1 * mm) });
        nfcDressPart(context, qCreatedBy(id + "kapton", EntityType.BODY), "Antenne NFC (Kapton)", color(0.85, 0.55, 0.15));

        // Fils vers le connecteur NFC de la carte
        fCylinder(context, id + "wireBlack", { "bottomCenter" : vector(W / 2 - 1 * mm, padY, wireZ), "topCenter" : vector(W / 2 - 1 * mm, -definition.wireLength, wireZ), "radius" : wireR });
        fCylinder(context, id + "wireRed", { "bottomCenter" : vector(W / 2 + 1 * mm, padY, wireZ), "topCenter" : vector(W / 2 + 1 * mm, -definition.wireLength, wireZ), "radius" : wireR });
        nfcDressPart(context, qCreatedBy(id + "wireBlack", EntityType.BODY), "Antenne NFC (fil noir)", color(0.1, 0.1, 0.1));
        nfcDressPart(context, qCreatedBy(id + "wireRed", EntityType.BODY), "Antenne NFC (fil rouge)", color(0.85, 0.1, 0.1));
    });

function nfcDressPart(context is Context, part is Query, name is string, appearance is Color)
{
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.NAME, "value" : name });
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.APPEARANCE, "value" : appearance });
}
