// Bande guide-cartes de la TotoBox: plaque collée contre la paroi extérieure de la fente à cartes, sur toute sa
// profondeur. Elle plaque la carte contre la cloison (donc le tag contre l'antenne NFC), cache les entretoises de
// fond par des encoches en bas à chaque bout, et son arête haute côté carte est biseautée pour faire entonnoir.
// Coller sous l'en-tête 'FeatureScript ...; import(...)' d'un Feature Studio dédié à ce fichier.
//
// Repère du Part Studio: la bande s'appuie sur la face "Paroi X" (face intérieure de la paroi) et s'épaissit vers
// +X, de "Y debut" à "Y fin" et de "Z bas" (face intérieure du fond) à "Z haut".

const GUIDE_POSITION = { (millimeter) : [-10000, 0, 10000] } as LengthBoundSpec;
const GUIDE_THICKNESS = { (millimeter) : [0.5, 3, 20] } as LengthBoundSpec;
const GUIDE_LAND = { (millimeter) : [0, 0.4, 5] } as LengthBoundSpec;
const GUIDE_NOTCH = { (millimeter) : [0, 4.2, 50] } as LengthBoundSpec;
const GUIDE_ANGLE = { (degree) : [20, 60, 85] } as AngleBoundSpec;

annotation { "Feature Type Name" : "Bande guide-cartes TotoBox", "Feature Type Description" : "Plaque contre la paroi de la fente a cartes: plaque la carte contre la cloison, biseau d'entree en haut, encoches pour les entretoises en bas a chaque bout." }
export const totoboxCardGuide = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Paroi X (face interieure)" }
        isLength(definition.wallX, GUIDE_POSITION);
        annotation { "Name" : "Y debut" }
        isLength(definition.yStart, GUIDE_POSITION);
        annotation { "Name" : "Y fin" }
        isLength(definition.yEnd, GUIDE_POSITION);
        annotation { "Name" : "Z bas (face interieure du fond)" }
        isLength(definition.zBottom, GUIDE_POSITION);
        annotation { "Name" : "Z haut" }
        isLength(definition.zTop, GUIDE_POSITION);
        annotation { "Name" : "Epaisseur" }
        isLength(definition.thickness, GUIDE_THICKNESS);
        annotation { "Name" : "Biseau: angle avec l'horizontale" }
        isAngle(definition.chamferAngle, GUIDE_ANGLE);
        annotation { "Name" : "Biseau: plat laisse en haut" }
        isLength(definition.land, GUIDE_LAND);
        annotation { "Name" : "Encoches: longueur (en Y)" }
        isLength(definition.notchLength, GUIDE_NOTCH);
        annotation { "Name" : "Encoches: hauteur" }
        isLength(definition.notchHeight, GUIDE_NOTCH);
    }
    {
        const mm = millimeter;
        const x0 = definition.wallX;
        const t = definition.thickness;
        const y0 = min(definition.yStart, definition.yEnd);
        const y1 = max(definition.yStart, definition.yEnd);
        const z0 = definition.zBottom;
        const z1 = definition.zTop;
        if (definition.land >= t)
        {
            throw regenError("Le plat du biseau doit etre plus fin que la bande.");
        }

        fCuboid(context, id + "bar", { "corner1" : vector(x0, y0, z0), "corner2" : vector(x0 + t, y1, z1) });

        // Biseau: on retire tout ce qui est au-dessus du plan qui part du haut (à "plat" de la paroi) et descend
        // vers la carte avec l'angle demandé. Bloc construit à plat puis tourné autour de l'axe Y.
        const pivot = vector(x0 + definition.land, 0 * mm, z1);
        fCuboid(context, id + "cutter", { "corner1" : vector(x0 + definition.land, y0 - 1 * mm, z1), "corner2" : vector(x0 + definition.land + 50 * mm, y1 + 1 * mm, z1 + 50 * mm) });
        opTransform(context, id + "tilt", { "bodies" : qCreatedBy(id + "cutter", EntityType.BODY), "transform" : rotationAround(line(pivot, vector(0, 1, 0)), definition.chamferAngle) });
        opBoolean(context, id + "chamfer", { "tools" : qCreatedBy(id + "cutter", EntityType.BODY), "targets" : qCreatedBy(id + "bar", EntityType.BODY), "operationType" : BooleanOperationType.SUBTRACTION });

        // Encoches en bas à chaque bout (entretoises de fond collées dans les coins)
        if (definition.notchLength > 0 * mm && definition.notchHeight > 0 * mm)
        {
            fCuboid(context, id + "notchStart", { "corner1" : vector(x0 - 1 * mm, y0 - 1 * mm, z0 - 1 * mm), "corner2" : vector(x0 + t + 1 * mm, y0 + definition.notchLength, z0 + definition.notchHeight) });
            fCuboid(context, id + "notchEnd", { "corner1" : vector(x0 - 1 * mm, y1 - definition.notchLength, z0 - 1 * mm), "corner2" : vector(x0 + t + 1 * mm, y1 + 1 * mm, z0 + definition.notchHeight) });
            opBoolean(context, id + "notches", { "tools" : qUnion([qCreatedBy(id + "notchStart", EntityType.BODY), qCreatedBy(id + "notchEnd", EntityType.BODY)]), "targets" : qCreatedBy(id + "bar", EntityType.BODY), "operationType" : BooleanOperationType.SUBTRACTION });
        }

        setProperty(context, { "entities" : qCreatedBy(id + "bar", EntityType.BODY), "propertyType" : PropertyType.NAME, "value" : "Box Card Guide" });
        setProperty(context, { "entities" : qCreatedBy(id + "bar", EntityType.BODY), "propertyType" : PropertyType.APPEARANCE, "value" : color(0.95, 0.6, 0.2) });
    }, { "chamferAngle" : 60 * degree, "land" : 0.4 * millimeter, "notchLength" : 4.2 * millimeter, "notchHeight" : 10.5 * millimeter });
