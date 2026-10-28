// Entretoise hexagonale M2 en laiton de la TotoBox (kit "M2 x 4 + 3 mm" mâle-femelle, ou femelle-femelle), avec
// le perçage de la pièce qui la reçoit.
// Coller sous l'en-tête 'FeatureScript ...; import(...)' d'un Feature Studio dédié à ce fichier.
//
// Repère: "Position" = centre de la base de l'entretoise, posée sur la face intérieure du fond; le corps monte
// vers +Z. Deux usages:
//  - carte électronique: entretoise mâle-femelle vissée dans un avant-trou du fond (perçage "Avant-trou"), la carte
//    vissée dessus (M2);
//  - fermeture du fond: entretoise femelle-femelle collée dans un coin des parois, vis M2 Torx tête fraisée passée
//    par le fond (perçage "Trou fraise", depuis la face extérieure du fond).
// L'hexagone est représenté par son encombrement (cylindre au diamètre sur angles).

export enum StandoffHole
{
    annotation { "Name" : "Aucun" }
    NONE,
    annotation { "Name" : "Avant-trou (filetage male)" }
    PILOT,
    annotation { "Name" : "Trou fraise (vis par le dessous)" }
    COUNTERSUNK
}

const STD_POSITION = { (millimeter) : [-10000, 0, 10000] } as LengthBoundSpec;
const STD_BODY = { (millimeter) : [1, 4, 60] } as LengthBoundSpec;
const STD_MALE = { (millimeter) : [0, 3, 20] } as LengthBoundSpec;
const STD_FLATS = { (millimeter) : [2, 3.2, 10] } as LengthBoundSpec;
const STD_PLATE = { (millimeter) : [0.5, 3, 20] } as LengthBoundSpec;
const STD_PILOT = { (millimeter) : [0.5, 1.6, 6] } as LengthBoundSpec;
const STD_CLEARANCE = { (millimeter) : [0.5, 2.2, 6] } as LengthBoundSpec;
const STD_HEAD = { (millimeter) : [1, 4.4, 12] } as LengthBoundSpec;

annotation { "Feature Type Name" : "Entretoise M2 TotoBox", "Feature Type Description" : "Entretoise hexagonale M2 posee sur le fond (corps vers +Z) et percage de la piece: avant-trou pour le filetage male, ou trou fraise pour une vis par le dessous." }
export const totoboxStandoff = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Position X (centre de la base)" }
        isLength(definition.positionX, STD_POSITION);
        annotation { "Name" : "Position Y" }
        isLength(definition.positionY, STD_POSITION);
        annotation { "Name" : "Position Z (face interieure du fond)" }
        isLength(definition.positionZ, STD_POSITION);
        annotation { "Name" : "Longueur du corps" }
        isLength(definition.bodyLength, STD_BODY);
        annotation { "Name" : "Longueur du filetage male (0 = femelle-femelle)" }
        isLength(definition.maleLength, STD_MALE);
        annotation { "Name" : "Cote sur plats" }
        isLength(definition.hexFlats, STD_FLATS);

        annotation { "Name" : "Percage" }
        definition.hole is StandoffHole;
        if (definition.hole != StandoffHole.NONE)
        {
            annotation { "Name" : "Piece a percer", "Filter" : EntityType.BODY && BodyType.SOLID, "MaxNumberOfPicks" : 1 }
            definition.target is Query;
            annotation { "Name" : "Epaisseur du fond" }
            isLength(definition.plateThickness, STD_PLATE);
        }
        if (definition.hole == StandoffHole.PILOT)
        {
            annotation { "Name" : "Diametre de l'avant-trou" }
            isLength(definition.pilotDiameter, STD_PILOT);
        }
        if (definition.hole == StandoffHole.COUNTERSUNK)
        {
            annotation { "Name" : "Diametre de passage de la vis" }
            isLength(definition.clearanceDiameter, STD_CLEARANCE);
            annotation { "Name" : "Diametre de la fraisure (tete)" }
            isLength(definition.headDiameter, STD_HEAD);
        }
    }
    {
        const mm = millimeter;
        const base = vector(definition.positionX, definition.positionY, definition.positionZ);
        const up = vector(0, 0, 1);
        const radius = definition.hexFlats / cos(30 * degree) / 2;

        fCylinder(context, id + "body", { "bottomCenter" : base, "topCenter" : base + up * definition.bodyLength, "radius" : radius });
        stdDressPart(context, qCreatedBy(id + "body", EntityType.BODY), "Entretoise M2 (encombrement)", color(0.85, 0.7, 0.25));
        if (definition.maleLength > 0 * mm)
        {
            fCylinder(context, id + "male", { "bottomCenter" : base - up * definition.maleLength, "topCenter" : base, "radius" : 1 * mm });
            stdDressPart(context, qCreatedBy(id + "male", EntityType.BODY), "Entretoise M2 (filetage male)", color(0.85, 0.7, 0.25));
        }

        if (definition.hole == StandoffHole.NONE)
        {
            return;
        }
        if (isQueryEmpty(context, definition.target))
        {
            throw regenError("Choisir la piece a percer (le fond).", ["target"]);
        }
        const plate = definition.plateThickness;
        if (definition.hole == StandoffHole.PILOT)
        {
            // Avant-trou borgne pour le filetage mâle, sans déboucher sous le fond s'il est plus épais
            const depth = min([definition.maleLength + 0.5 * mm, plate]);
            fCylinder(context, id + "pilot", { "bottomCenter" : base - up * depth, "topCenter" : base + up * 0.5 * mm, "radius" : definition.pilotDiameter / 2 });
            opBoolean(context, id + "pilotCut", { "tools" : qCreatedBy(id + "pilot", EntityType.BODY), "targets" : definition.target, "operationType" : BooleanOperationType.SUBTRACTION });
        }
        else
        {
            // Passage de la vis et fraisure à 90° depuis la face extérieure du fond
            const outside = base - up * plate;
            const sink = (definition.headDiameter - definition.clearanceDiameter) / 2;
            fCylinder(context, id + "clearance", { "bottomCenter" : outside - up * 0.5 * mm, "topCenter" : base + up * 0.5 * mm, "radius" : definition.clearanceDiameter / 2 });
            fCone(context, id + "sink", { "bottomCenter" : outside - up * 0.5 * mm, "topCenter" : outside + up * sink, "bottomRadius" : definition.headDiameter / 2 + 0.5 * mm, "topRadius" : definition.clearanceDiameter / 2 });
            opBoolean(context, id + "holeCut", { "tools" : qUnion([qCreatedBy(id + "clearance", EntityType.BODY), qCreatedBy(id + "sink", EntityType.BODY)]), "targets" : definition.target, "operationType" : BooleanOperationType.SUBTRACTION });
        }
    }, { "hole" : StandoffHole.NONE, "pilotDiameter" : 1.6 * millimeter, "clearanceDiameter" : 2.2 * millimeter,
         "headDiameter" : 4.4 * millimeter, "plateThickness" : 3 * millimeter });

function stdDressPart(context is Context, part is Query, name is string, appearance is Color)
{
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.NAME, "value" : name });
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.APPEARANCE, "value" : appearance });
}
