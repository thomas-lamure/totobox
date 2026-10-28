// Plaque de contreplaqué pour le calepinage des pièces de 3 mm de la TotoBox (lot de plaques 300 x 200 x 3 mm).
// Coller sous l'en-tête 'FeatureScript ...; import(...)' d'un Feature Studio dédié à ce fichier.
//
// Repère: coin de la plaque à l'origine, plaque dans le plan XY de Z = 0 à Z = épaisseur. Les pièces se posent
// dessus (Z = épaisseur) dans l'assemblage de calepinage.

const SHEET_LENGTH = { (millimeter) : [10, 300, 3000] } as LengthBoundSpec;
const SHEET_WIDTH = { (millimeter) : [10, 200, 3000] } as LengthBoundSpec;
const SHEET_THICKNESS = { (millimeter) : [0.5, 3, 50] } as LengthBoundSpec;

annotation { "Feature Type Name" : "Plaque contreplaque TotoBox", "Feature Type Description" : "Plaque de contreplaque pour le calepinage: coin a l'origine, dans le plan XY, epaisseur vers +Z." }
export const totoboxSheet = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Longueur (X)" }
        isLength(definition.length, SHEET_LENGTH);
        annotation { "Name" : "Largeur (Y)" }
        isLength(definition.width, SHEET_WIDTH);
        annotation { "Name" : "Epaisseur" }
        isLength(definition.thickness, SHEET_THICKNESS);
    }
    {
        fCuboid(context, id + "sheet", { "corner1" : vector(0, 0, 0) * millimeter, "corner2" : vector(definition.length, definition.width, definition.thickness) });
        setProperty(context, { "entities" : qCreatedBy(id + "sheet", EntityType.BODY), "propertyType" : PropertyType.NAME, "value" : "Plaque contreplaque 300x200x3" });
        setProperty(context, { "entities" : qCreatedBy(id + "sheet", EntityType.BODY), "propertyType" : PropertyType.APPEARANCE, "value" : color(0.87, 0.76, 0.58, 0.6) });
    });
