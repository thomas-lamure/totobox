// Encodeur rotatif KY-040 (module PCB + encodeur EC11 à poussoir) de la TotoBox: modèle simplifié pour dessiner
// le boîtier. Cotes par défaut: fiche du vendeur (26 x 19 x 29 mm, filetage M7 de 7 mm, bouton Ø 15 x 16,5 mm) et
// cotes usuelles de l'EC11; toutes réglables dans la Feature, à vérifier au pied à coulisse.
// Coller sous l'en-tête 'FeatureScript ...; import(...)' d'un Feature Studio dédié à ce fichier.
//
// Repère local: origine sur l'axe de l'arbre, sur la face d'appui du canon fileté (dessus du corps de l'encodeur),
// c'est-à-dire la face intérieure de la paroi quand l'encodeur est monté par son écrou. Arbre et bouton vers +Z,
// circuit imprimé vers -Z, broches vers +X. La paroi ne doit pas dépasser (longueur du canon - épaisseur de l'écrou).
//
// Placement: "Position" = ce point dans le Part Studio (centre du trou sur la face intérieure de la paroi),
// "Arbre vers" = direction de l'arbre (vers l'extérieur de la paroi), "Broches vers" = direction des broches
// (perpendiculaire à l'arbre). Les positions acceptent des variables (#encodeur_y...).

export enum EncoderDirection
{
    annotation { "Name" : "+X" }
    X_PLUS,
    annotation { "Name" : "-X" }
    X_MINUS,
    annotation { "Name" : "+Y" }
    Y_PLUS,
    annotation { "Name" : "-Y" }
    Y_MINUS,
    annotation { "Name" : "+Z" }
    Z_PLUS,
    annotation { "Name" : "-Z" }
    Z_MINUS
}

const ENC_POSITION = { (millimeter) : [-10000, 0, 10000] } as LengthBoundSpec;

const ENC_PCB_LENGTH = { (millimeter) : [15, 26, 60] } as LengthBoundSpec;
const ENC_PCB_WIDTH = { (millimeter) : [12, 19, 60] } as LengthBoundSpec;
const ENC_PCB_THICKNESS = { (millimeter) : [0.8, 1.6, 3] } as LengthBoundSpec;
const ENC_AXIS_FROM_EDGE = { (millimeter) : [5, 10, 40] } as LengthBoundSpec;
const ENC_BODY_SIZE = { (millimeter) : [8, 12.4, 20] } as LengthBoundSpec;
const ENC_BODY_HEIGHT = { (millimeter) : [3, 6.5, 15] } as LengthBoundSpec;
const ENC_BUSHING_DIAMETER = { (millimeter) : [5, 7, 12] } as LengthBoundSpec;
const ENC_BUSHING_LENGTH = { (millimeter) : [2, 7, 20] } as LengthBoundSpec;
const ENC_TOTAL_HEIGHT = { (millimeter) : [15, 29, 50] } as LengthBoundSpec;
const ENC_SHAFT_DIAMETER = { (millimeter) : [3, 6, 10] } as LengthBoundSpec;
const ENC_NUT_FLATS = { (millimeter) : [7, 10, 16] } as LengthBoundSpec;
const ENC_NUT_THICKNESS = { (millimeter) : [0.5, 2, 5] } as LengthBoundSpec;
const ENC_PANEL = { (millimeter) : [0, 3, 15] } as LengthBoundSpec;
const ENC_KNOB_DIAMETER = { (millimeter) : [8, 15, 50] } as LengthBoundSpec;
const ENC_KNOB_HEIGHT = { (millimeter) : [5, 16.5, 50] } as LengthBoundSpec;
const ENC_KNOB_GAP = { (millimeter) : [0, 1, 10] } as LengthBoundSpec;
const ENC_PIN_LENGTH = { (millimeter) : [1, 6, 20] } as LengthBoundSpec;

annotation { "Feature Type Name" : "Encodeur KY-040", "Feature Type Description" : "Module encodeur rotatif KY-040 simplifie, place par la position du centre du trou sur la face interieure de la paroi et les directions de l'arbre et des broches." }
export const totoboxEncoder = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Longueur du circuit (axe des broches)" }
        isLength(definition.pcbLength, ENC_PCB_LENGTH);
        annotation { "Name" : "Largeur du circuit" }
        isLength(definition.pcbWidth, ENC_PCB_WIDTH);
        annotation { "Name" : "Epaisseur du circuit" }
        isLength(definition.pcbThickness, ENC_PCB_THICKNESS);
        annotation { "Name" : "Axe - bord oppose aux broches" }
        isLength(definition.axisFromEdge, ENC_AXIS_FROM_EDGE);
        annotation { "Name" : "Cote du corps de l'encodeur" }
        isLength(definition.bodySize, ENC_BODY_SIZE);
        annotation { "Name" : "Hauteur du corps (au-dessus du circuit)" }
        isLength(definition.bodyHeight, ENC_BODY_HEIGHT);
        annotation { "Name" : "Diametre du canon filete" }
        isLength(definition.bushingDiameter, ENC_BUSHING_DIAMETER);
        annotation { "Name" : "Longueur du canon filete" }
        isLength(definition.bushingLength, ENC_BUSHING_LENGTH);
        annotation { "Name" : "Hauteur totale (dessous du circuit - bout de l'arbre)" }
        isLength(definition.totalHeight, ENC_TOTAL_HEIGHT);
        annotation { "Name" : "Diametre de l'arbre" }
        isLength(definition.shaftDiameter, ENC_SHAFT_DIAMETER);

        annotation { "Name" : "Ecrou et paroi" }
        definition.showNut is boolean;
        if (definition.showNut)
        {
            annotation { "Name" : "Ecrou: cote sur plats (modele au diametre sur angles)" }
            isLength(definition.nutFlats, ENC_NUT_FLATS);
            annotation { "Name" : "Ecrou: epaisseur" }
            isLength(definition.nutThickness, ENC_NUT_THICKNESS);
            annotation { "Name" : "Epaisseur de paroi (ecrou pose dessus)" }
            isLength(definition.panelThickness, ENC_PANEL);
        }

        annotation { "Name" : "Bouton" }
        definition.showKnob is boolean;
        if (definition.showKnob)
        {
            annotation { "Name" : "Bouton: diametre" }
            isLength(definition.knobDiameter, ENC_KNOB_DIAMETER);
            annotation { "Name" : "Bouton: hauteur" }
            isLength(definition.knobHeight, ENC_KNOB_HEIGHT);
            annotation { "Name" : "Bouton: jeu au-dessus de la paroi" }
            isLength(definition.knobGap, ENC_KNOB_GAP);
        }

        annotation { "Name" : "Longueur des broches hors du circuit" }
        isLength(definition.pinLength, ENC_PIN_LENGTH);

        annotation { "Name" : "Position X (centre du trou, face interieure)" }
        isLength(definition.positionX, ENC_POSITION);
        annotation { "Name" : "Position Y" }
        isLength(definition.positionY, ENC_POSITION);
        annotation { "Name" : "Position Z" }
        isLength(definition.positionZ, ENC_POSITION);
        annotation { "Name" : "Arbre vers" }
        definition.shaftDirection is EncoderDirection;
        annotation { "Name" : "Broches vers" }
        definition.pinsDirection is EncoderDirection;
    }
    {
        const mm = millimeter;
        const L = definition.pcbLength;
        const W = definition.pcbWidth;
        const t = definition.pcbThickness;
        const h = definition.bodyHeight;
        const s = definition.bodySize;
        // Circuit sous le corps: dessus du circuit en Z = -h, dessous en Z = -h - t
        const pcbTop = -h;
        const pcbBottom = -h - t;
        // Axe à axisFromEdge du bord -X (opposé aux broches), centré en largeur
        const xMin = -definition.axisFromEdge;
        const xMax = L - definition.axisFromEdge;
        if (definition.axisFromEdge + s / 2 > L)
        {
            throw regenError("L'encodeur depasse du circuit: reduire la distance axe - bord.");
        }
        const shaftTip = pcbBottom + definition.totalHeight;
        const bushingTop = definition.bushingLength;
        if (shaftTip <= bushingTop)
        {
            throw regenError("La hauteur totale doit laisser l'arbre depasser du canon.");
        }

        // Circuit imprimé
        fCuboid(context, id + "pcb", { "corner1" : vector(xMin, -W / 2, pcbBottom), "corner2" : vector(xMax, W / 2, pcbTop) });
        encDressPart(context, qCreatedBy(id + "pcb", EntityType.BODY), "Encodeur (circuit)", color(0.1, 0.3, 0.6));

        // Corps de l'encodeur (cadre métallique et boîtier)
        fCuboid(context, id + "body", { "corner1" : vector(-s / 2, -s / 2, pcbTop), "corner2" : vector(s / 2, s / 2, 0 * mm) });
        encDressPart(context, qCreatedBy(id + "body", EntityType.BODY), "Encodeur (corps)", color(0.75, 0.75, 0.78));

        // Canon fileté (traverse la paroi)
        fCylinder(context, id + "bushing", { "bottomCenter" : vector(0, 0, 0) * mm, "topCenter" : vector(0 * mm, 0 * mm, bushingTop), "radius" : definition.bushingDiameter / 2 });
        encDressPart(context, qCreatedBy(id + "bushing", EntityType.BODY), "Encodeur (canon M7)", color(0.8, 0.8, 0.82));

        // Arbre à méplat (D): cylindre moins un plat sur la moitié haute
        const r = definition.shaftDiameter / 2;
        fCylinder(context, id + "shaft", { "bottomCenter" : vector(0 * mm, 0 * mm, bushingTop), "topCenter" : vector(0 * mm, 0 * mm, shaftTip), "radius" : r });
        const flatStart = bushingTop + (shaftTip - bushingTop) / 2;
        fCuboid(context, id + "flat", { "corner1" : vector(r * 0.5, -r - 1 * mm, flatStart), "corner2" : vector(r + 1 * mm, r + 1 * mm, shaftTip + 1 * mm) });
        opBoolean(context, id + "flatCut", { "tools" : qCreatedBy(id + "flat", EntityType.BODY), "targets" : qCreatedBy(id + "shaft", EntityType.BODY), "operationType" : BooleanOperationType.SUBTRACTION });
        encDressPart(context, qCreatedBy(id + "shaft", EntityType.BODY), "Encodeur (arbre)", color(0.85, 0.85, 0.87));

        // Barrette 5 broches coudée sur le bord +X (GND, +, SW, DT, CLK), broches à plat vers +X
        const pitch = 2.54 * mm;
        fCuboid(context, id + "header", { "corner1" : vector(xMax - 2.5 * mm, -2.5 * pitch, pcbTop), "corner2" : vector(xMax, 2.5 * pitch, pcbTop + 2.5 * mm) });
        encDressPart(context, qCreatedBy(id + "header", EntityType.BODY), "Encodeur (barrette)", color(0.08, 0.08, 0.08));
        for (var i = 0; i < 5; i += 1)
        {
            const y = (i - 2) * pitch;
            fCuboid(context, id + ("pin" ~ i), { "corner1" : vector(xMax - 2 * mm, y - 0.32 * mm, pcbTop + 0.93 * mm), "corner2" : vector(xMax + definition.pinLength, y + 0.32 * mm, pcbTop + 1.57 * mm) });
            encDressPart(context, qCreatedBy(id + ("pin" ~ i), EntityType.BODY), "Encodeur (broche " ~ (i + 1) ~ ")", color(0.85, 0.7, 0.2));
        }

        var panelTop = 0 * mm;
        if (definition.showNut)
        {
            // Écrou posé sur la face extérieure de la paroi
            panelTop = definition.panelThickness;
            if (panelTop + definition.nutThickness > bushingTop)
            {
                throw regenError("Paroi trop epaisse: l'ecrou ne prend plus sur le canon.");
            }
            // Écrou hexagonal représenté par son encombrement: cylindre au diamètre sur angles (dégagement
            // à prévoir autour pour la clé), percé au diamètre du canon.
            const acrossCorners = definition.nutFlats / cos(30 * degree);
            fCylinder(context, id + "nut", { "bottomCenter" : vector(0 * mm, 0 * mm, panelTop), "topCenter" : vector(0 * mm, 0 * mm, panelTop + definition.nutThickness), "radius" : acrossCorners / 2 });
            fCylinder(context, id + "nutHole", { "bottomCenter" : vector(0 * mm, 0 * mm, panelTop - 1 * mm), "topCenter" : vector(0 * mm, 0 * mm, panelTop + definition.nutThickness + 1 * mm), "radius" : definition.bushingDiameter / 2 });
            opBoolean(context, id + "nutHoleCut", { "tools" : qCreatedBy(id + "nutHole", EntityType.BODY), "targets" : qCreatedBy(id + "nut", EntityType.BODY), "operationType" : BooleanOperationType.SUBTRACTION });
            encDressPart(context, qCreatedBy(id + "nut", EntityType.BODY), "Encodeur (ecrou M7)", color(0.7, 0.7, 0.72));
        }

        if (definition.showKnob)
        {
            // Bouton emmanché sur l'arbre, avec un jeu au-dessus de la paroi
            const knobBottom = panelTop + definition.knobGap;
            fCylinder(context, id + "knob", { "bottomCenter" : vector(0 * mm, 0 * mm, knobBottom), "topCenter" : vector(0 * mm, 0 * mm, knobBottom + definition.knobHeight), "radius" : definition.knobDiameter / 2 });
            encDressPart(context, qCreatedBy(id + "knob", EntityType.BODY), "Encodeur (bouton)", color(0.15, 0.15, 0.16));
        }

        // Placement: repère local (broches = X, arbre = Z) vers le Part Studio
        const shaftAxis = encDirectionVector(definition.shaftDirection);
        const pinsAxis = encDirectionVector(definition.pinsDirection);
        if (abs(dot(shaftAxis, pinsAxis)) > 0.5)
        {
            throw regenError("Les broches doivent etre perpendiculaires a l'arbre.");
        }
        const origin = vector(definition.positionX, definition.positionY, definition.positionZ);
        opTransform(context, id + "place", { "bodies" : qCreatedBy(id, EntityType.BODY), "transform" : toWorld(coordSystem(origin, pinsAxis, shaftAxis)) });
    }, { "positionX" : 0 * millimeter, "positionY" : 0 * millimeter, "positionZ" : 0 * millimeter,
         "shaftDirection" : EncoderDirection.Z_PLUS, "pinsDirection" : EncoderDirection.X_PLUS,
         "showNut" : true, "nutFlats" : 10 * millimeter, "nutThickness" : 2 * millimeter, "panelThickness" : 3 * millimeter,
         "showKnob" : true, "knobDiameter" : 15 * millimeter, "knobHeight" : 16.5 * millimeter, "knobGap" : 1 * millimeter });

function encDirectionVector(direction is EncoderDirection) returns Vector
{
    if (direction == EncoderDirection.X_PLUS)
        return vector(1, 0, 0);
    if (direction == EncoderDirection.X_MINUS)
        return vector(-1, 0, 0);
    if (direction == EncoderDirection.Y_PLUS)
        return vector(0, 1, 0);
    if (direction == EncoderDirection.Y_MINUS)
        return vector(0, -1, 0);
    if (direction == EncoderDirection.Z_PLUS)
        return vector(0, 0, 1);
    return vector(0, 0, -1);
}

function encDressPart(context is Context, part is Query, name is string, appearance is Color)
{
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.NAME, "value" : name });
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.APPEARANCE, "value" : appearance });
}
