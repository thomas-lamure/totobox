// Carte HermitX CardReader: modèle simplifié pour dessiner le boîtier TotoBox.
// Fichier généré par hardware/tools/hermitx_board.py: modifier les cotes là-bas, pas ici.
// Coller sous l'en-tête 'FeatureScript ...; import(...)' créé par Onshape dans un nouveau Feature Studio.

const PCB_THICKNESS = { (millimeter) : [0.4, 1.6, 5] } as LengthBoundSpec;

annotation { "Feature Type Name" : "Carte HermitX", "Feature Type Description" : "Carte HermitX CardReader simplifiee (99 x 62 mm), origine au coin inferieur gauche, Z = 0 sous le PCB." }
export const hermitxBoard = defineFeature(function(context is Context, id is Id, definition is map)
    precondition
    {
        annotation { "Name" : "Epaisseur du PCB" }
        isLength(definition.pcbThickness, PCB_THICKNESS);
    }
    {
        const t = definition.pcbThickness / millimeter;

        // PCB aux coins arrondis
        fCuboid(context, id + "pcb", { "corner1" : vector(0, 0, 0) * millimeter, "corner2" : vector(99, 62, t) * millimeter });
        opFillet(context, id + "pcbCorners", { "entities" : qParallelEdges(qCreatedBy(id + "pcb", EntityType.EDGE), Z_DIRECTION), "radius" : 4 * millimeter });

        // Trous de fixation et trous des connecteurs non soudés
        fCylinder(context, id + "holes" + "h0", { "bottomCenter" : vector(4, 4, -1) * millimeter, "topCenter" : vector(4, 4, t + 1) * millimeter, "radius" : 1.6 * millimeter });
        fCylinder(context, id + "holes" + "h1", { "bottomCenter" : vector(95, 4, -1) * millimeter, "topCenter" : vector(95, 4, t + 1) * millimeter, "radius" : 1.6 * millimeter });
        fCylinder(context, id + "holes" + "h2", { "bottomCenter" : vector(4, 58, -1) * millimeter, "topCenter" : vector(4, 58, t + 1) * millimeter, "radius" : 1.6 * millimeter });
        fCylinder(context, id + "holes" + "h3", { "bottomCenter" : vector(95, 58, -1) * millimeter, "topCenter" : vector(95, 58, t + 1) * millimeter, "radius" : 1.6 * millimeter });
        fCylinder(context, id + "holes" + "h4", { "bottomCenter" : vector(43.37, 20.2, -1) * millimeter, "topCenter" : vector(43.37, 20.2, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h5", { "bottomCenter" : vector(45.91, 20.2, -1) * millimeter, "topCenter" : vector(45.91, 20.2, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h6", { "bottomCenter" : vector(48.45, 20.2, -1) * millimeter, "topCenter" : vector(48.45, 20.2, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h7", { "bottomCenter" : vector(50.99, 20.2, -1) * millimeter, "topCenter" : vector(50.99, 20.2, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h8", { "bottomCenter" : vector(53.53, 20.2, -1) * millimeter, "topCenter" : vector(53.53, 20.2, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h9", { "bottomCenter" : vector(56.07, 20.2, -1) * millimeter, "topCenter" : vector(56.07, 20.2, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h10", { "bottomCenter" : vector(58.61, 20.2, -1) * millimeter, "topCenter" : vector(58.61, 20.2, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h11", { "bottomCenter" : vector(57.45, 47.3, -1) * millimeter, "topCenter" : vector(57.45, 47.3, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h12", { "bottomCenter" : vector(57.45, 44.76, -1) * millimeter, "topCenter" : vector(57.45, 44.76, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h13", { "bottomCenter" : vector(57.45, 42.22, -1) * millimeter, "topCenter" : vector(57.45, 42.22, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h14", { "bottomCenter" : vector(90.7, 18.1, -1) * millimeter, "topCenter" : vector(90.7, 18.1, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        fCylinder(context, id + "holes" + "h15", { "bottomCenter" : vector(93.24, 18.1, -1) * millimeter, "topCenter" : vector(93.24, 18.1, t + 1) * millimeter, "radius" : 0.5 * millimeter });
        opBoolean(context, id + "holesCut", { "tools" : qCreatedBy(id + "holes", EntityType.BODY), "targets" : qCreatedBy(id + "pcb", EntityType.BODY), "operationType" : BooleanOperationType.SUBTRACTION });
        dressPart(context, qCreatedBy(id + "pcb", EntityType.BODY), "PCB HermitX", color(0.07, 0.07, 0.08));

        // Composants (face supérieure)
        fCuboid(context, id + "c0", { "corner1" : vector(59.8, 55, t + 0) * millimeter, "corner2" : vector(68.8, 62.6, t + 3.2) * millimeter });
        dressPart(context, qCreatedBy(id + "c0", EntityType.BODY), "USB-C", color(0.75, 0.76, 0.78));
        fCuboid(context, id + "c1", { "corner1" : vector(75.2, 43, t + 0) * millimeter, "corner2" : vector(90.8, 60.2, t + 1.9) * millimeter });
        dressPart(context, qCreatedBy(id + "c1", EntityType.BODY), "Support microSD", color(0.75, 0.76, 0.78));
        fCuboid(context, id + "c2", { "corner1" : vector(78, 21.25, t + 0) * millimeter, "corner2" : vector(103.5, 39.25, t + 0.8) * millimeter });
        dressPart(context, qCreatedBy(id + "c2", EntityType.BODY), "Module ESP32-S3 (PCB)", color(0.1, 0.12, 0.25));
        fCuboid(context, id + "c3", { "corner1" : vector(79.2, 22.5, t + 0.8) * millimeter, "corner2" : vector(96.6, 38, t + 3.1) * millimeter });
        dressPart(context, qCreatedBy(id + "c3", EntityType.BODY), "Module ESP32-S3 (blindage)", color(0.82, 0.83, 0.85));
        fCuboid(context, id + "c4", { "corner1" : vector(97.2, 22, t + 0.8) * millimeter, "corner2" : vector(103, 38.5, t + 0.85) * millimeter });
        dressPart(context, qCreatedBy(id + "c4", EntityType.BODY), "Module ESP32-S3 (zone antenne)", color(0.85, 0.65, 0.2));
        fCuboid(context, id + "c5", { "corner1" : vector(1.4, 26.9, t + 0) * millimeter, "corner2" : vector(6, 35.2, t + 4.5) * millimeter });
        dressPart(context, qCreatedBy(id + "c5", EntityType.BODY), "Connecteur NFC", color(0.93, 0.93, 0.9));
        fCuboid(context, id + "c6", { "corner1" : vector(44.3, 54.8, t + 0) * millimeter, "corner2" : vector(52.8, 60.5, t + 4.5) * millimeter });
        dressPart(context, qCreatedBy(id + "c6", EntityType.BODY), "Connecteur haut-parleur", color(0.93, 0.93, 0.9));
        fCuboid(context, id + "c7", { "corner1" : vector(67.9, 34.6, t + 0) * millimeter, "corner2" : vector(73.9, 40.6, t + 3.5) * millimeter });
        dressPart(context, qCreatedBy(id + "c7", EntityType.BODY), "Bouton RESET", color(0.35, 0.35, 0.38));
        fCuboid(context, id + "c8", { "corner1" : vector(67.9, 18.9, t + 0) * millimeter, "corner2" : vector(73.9, 24.9, t + 3.5) * millimeter });
        dressPart(context, qCreatedBy(id + "c8", EntityType.BODY), "Bouton BOOT", color(0.35, 0.35, 0.38));
        fCuboid(context, id + "c9", { "corner1" : vector(46.4, 38.6, t + 0) * millimeter, "corner2" : vector(54.3, 44.8, t + 1.7) * millimeter });
        dressPart(context, qCreatedBy(id + "c9", EntityType.BODY), "Regulateur U2", color(0.08, 0.08, 0.09));
        fCuboid(context, id + "c10", { "corner1" : vector(25.6, 27.5, t + 0) * millimeter, "corner2" : vector(32, 34.4, t + 0.9) * millimeter });
        dressPart(context, qCreatedBy(id + "c10", EntityType.BODY), "PN532", color(0.08, 0.08, 0.09));
        fCuboid(context, id + "c11", { "corner1" : vector(42.7, 48.6, t + 0) * millimeter, "corner2" : vector(46, 51.9, t + 0.8) * millimeter });
        dressPart(context, qCreatedBy(id + "c11", EntityType.BODY), "Ampli MAX98357", color(0.08, 0.08, 0.09));
        fCuboid(context, id + "c12", { "corner1" : vector(28.25, 23.3, t + 0) * millimeter, "corner2" : vector(31.8, 26.1, t + 0.8) * millimeter });
        dressPart(context, qCreatedBy(id + "c12", EntityType.BODY), "Quartz", color(0.75, 0.76, 0.78));
        fCuboid(context, id + "c13", { "corner1" : vector(6.6, 5, t + 0) * millimeter, "corner2" : vector(11.6, 10, t + 1.6) * millimeter });
        dressPart(context, qCreatedBy(id + "c13", EntityType.BODY), "LED 1", color(0.96, 0.96, 0.92));
        fCuboid(context, id + "c14", { "corner1" : vector(18.1, 5, t + 0) * millimeter, "corner2" : vector(23.1, 10, t + 1.6) * millimeter });
        dressPart(context, qCreatedBy(id + "c14", EntityType.BODY), "LED 2", color(0.96, 0.96, 0.92));
        fCuboid(context, id + "c15", { "corner1" : vector(29.6, 5, t + 0) * millimeter, "corner2" : vector(34.6, 10, t + 1.6) * millimeter });
        dressPart(context, qCreatedBy(id + "c15", EntityType.BODY), "LED 3", color(0.96, 0.96, 0.92));
        fCuboid(context, id + "c16", { "corner1" : vector(41, 5, t + 0) * millimeter, "corner2" : vector(46, 10, t + 1.6) * millimeter });
        dressPart(context, qCreatedBy(id + "c16", EntityType.BODY), "LED 4", color(0.96, 0.96, 0.92));
        fCuboid(context, id + "c17", { "corner1" : vector(52.4, 5, t + 0) * millimeter, "corner2" : vector(57.4, 10, t + 1.6) * millimeter });
        dressPart(context, qCreatedBy(id + "c17", EntityType.BODY), "LED 5", color(0.96, 0.96, 0.92));
        fCuboid(context, id + "c18", { "corner1" : vector(63.9, 5, t + 0) * millimeter, "corner2" : vector(68.9, 10, t + 1.6) * millimeter });
        dressPart(context, qCreatedBy(id + "c18", EntityType.BODY), "LED 6", color(0.96, 0.96, 0.92));
        fCuboid(context, id + "c19", { "corner1" : vector(75.4, 5, t + 0) * millimeter, "corner2" : vector(80.4, 10, t + 1.6) * millimeter });
        dressPart(context, qCreatedBy(id + "c19", EntityType.BODY), "LED 7", color(0.96, 0.96, 0.92));
        fCuboid(context, id + "c20", { "corner1" : vector(86.8, 5, t + 0) * millimeter, "corner2" : vector(91.8, 10, t + 1.6) * millimeter });
        dressPart(context, qCreatedBy(id + "c20", EntityType.BODY), "LED 8", color(0.96, 0.96, 0.92));
        fCylinder(context, id + "p0", { "bottomCenter" : vector(70.9, 37.6, t + 3.5) * millimeter, "topCenter" : vector(70.9, 37.6, t + 5) * millimeter, "radius" : 1.75 * millimeter });
        dressPart(context, qCreatedBy(id + "p0", EntityType.BODY), "Poussoir RESET", color(0.08, 0.08, 0.09));
        fCylinder(context, id + "p1", { "bottomCenter" : vector(70.9, 21.9, t + 3.5) * millimeter, "topCenter" : vector(70.9, 21.9, t + 5) * millimeter, "radius" : 1.75 * millimeter });
        dressPart(context, qCreatedBy(id + "p1", EntityType.BODY), "Poussoir BOOT", color(0.08, 0.08, 0.09));

        // Barrettes mâles: socle noir et broches traversantes dorées
        fCuboid(context, id + "hb0", { "corner1" : vector(42.1, 18.93, t) * millimeter, "corner2" : vector(59.88, 21.47, t + 2.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hb0", EntityType.BODY), "Barrette IO (socle)", color(0.08, 0.08, 0.09));
        fCuboid(context, id + "hp0_0", { "corner1" : vector(43.05, 19.88, -3) * millimeter, "corner2" : vector(43.69, 20.52, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp0_0", EntityType.BODY), "Broche IO9", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hp0_1", { "corner1" : vector(45.59, 19.88, -3) * millimeter, "corner2" : vector(46.23, 20.52, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp0_1", EntityType.BODY), "Broche IO10", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hp0_2", { "corner1" : vector(48.13, 19.88, -3) * millimeter, "corner2" : vector(48.77, 20.52, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp0_2", EntityType.BODY), "Broche IO11", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hp0_3", { "corner1" : vector(50.67, 19.88, -3) * millimeter, "corner2" : vector(51.31, 20.52, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp0_3", EntityType.BODY), "Broche IO45", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hp0_4", { "corner1" : vector(53.21, 19.88, -3) * millimeter, "corner2" : vector(53.85, 20.52, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp0_4", EntityType.BODY), "Broche IO46", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hp0_5", { "corner1" : vector(55.75, 19.88, -3) * millimeter, "corner2" : vector(56.39, 20.52, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp0_5", EntityType.BODY), "Broche IO47", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hp0_6", { "corner1" : vector(58.29, 19.88, -3) * millimeter, "corner2" : vector(58.93, 20.52, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp0_6", EntityType.BODY), "Broche IO48", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hb1", { "corner1" : vector(56.18, 40.95, t) * millimeter, "corner2" : vector(58.72, 48.57, t + 2.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hb1", EntityType.BODY), "Barrette alimentation (socle)", color(0.08, 0.08, 0.09));
        fCuboid(context, id + "hp1_0", { "corner1" : vector(57.13, 46.98, -3) * millimeter, "corner2" : vector(57.77, 47.62, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp1_0", EntityType.BODY), "Broche +5V", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hp1_1", { "corner1" : vector(57.13, 44.44, -3) * millimeter, "corner2" : vector(57.77, 45.08, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp1_1", EntityType.BODY), "Broche +3V3", color(0.9, 0.75, 0.3));
        fCuboid(context, id + "hp1_2", { "corner1" : vector(57.13, 41.9, -3) * millimeter, "corner2" : vector(57.77, 42.54, t + 8.5) * millimeter });
        dressPart(context, qCreatedBy(id + "hp1_2", EntityType.BODY), "Broche GND", color(0.9, 0.75, 0.3));
    });

function dressPart(context is Context, part is Query, name is string, appearance is Color)
{
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.NAME, "value" : name });
    setProperty(context, { "entities" : part, "propertyType" : PropertyType.APPEARANCE, "value" : appearance });
}
