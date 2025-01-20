CREATE TABLE IF NOT EXISTS intrebari (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    intrebare TEXT NOT NULL,
    varianta_A TEXT NOT NULL,
    varianta_B TEXT NOT NULL,
    varianta_C TEXT NOT NULL,
    varianta_D TEXT NOT NULL,
    varianta_corecta TEXT NOT NULL,
    dificultate TEXT NOT NULL
);

INSERT INTO intrebari (intrebare, varianta_A, varianta_B, varianta_C, varianta_D, varianta_corecta, dificultate) VALUES
('Unul dintre cei mai periculosi vulcani ai Europei se afla in Italia. Cum se numeste?', 'Krakatau', 'Vezuviu', 'Etna', 'Stromboli', 'B', 'Easy'),
('Vanturile sunt provocate de deplasarea aerului cu diferite viteze. Ce se foloseste pentru a le masura intensitatea?', 'scara Mercalli', 'scara Richter', 'scara Windsor', 'scara Beaufort', 'D', 'Easy'),
('Specialistii care studiaza animalele se numesc zoologi, dar exista si specializari in studiul cate unui singur grup de animale. Cum se numesc cei care studiaza reptilele?', 'serpentologi', 'ihtiologi', 'herpetologi', 'ornitologi', 'C', 'Easy'),
('Tundra este un mediu foarte rece in care mare parte din sol ramane inghetat in profunzime chiar si vara. Terenul acesta vesnic inghetat poarta un nume special. Care?', 'avalansa', 'cernoziom', 'spedosol', 'permafrost', 'D', 'Easy'),
('Mineralele se definesc printr-o trasatura specifica, duritatea. Care este cel mai dur mineral din lume?', 'cuartul', 'pirita', 'talcul', 'diamantul', 'D', 'Easy');

INSERT INTO intrebari (intrebare, varianta_A, varianta_B, varianta_C, varianta_D, varianta_corecta, dificultate) VALUES
('Toate deserturile sunt aride, insa nu toate sunt fierbinti. In Asia exista un desert rece. Cum se numeste?', 'Gobi', 'Mojave', 'Sahara', 'Lut', 'A', 'Medium'),
('Prada capturata de un leu poate oferi un pranz excelent si altor animale: hiene, sacali, vulturi sau insecte. Ce tip de relatie ilustreaza acest exemplu?', 'predatorism', 'mutualism', 'comensualism', 'parazitism', 'C', 'Medium'),
('Ierburile savanei furnizeaza hrana multor animale, printre care si elefantul, cel mai mare animal terestru. Cate ore pe zi petrece mancand?', '4 ore', '8 ore', '16 ore', '24 de ore', 'C', 'Medium'),
('Cand se vorbeste despre cutremure, se face adesea referire la locul de la suprafata pamantului aflat chiar deasupra focarului seismic din adancuri. Cum se numeste acest loc?', 'epicentru', 'ipocentru', 'semicentru', 'baricentru', 'A', 'Medium'),
('Padurile tropicale sunt habitatul tipic al orhideelor. Ce tip de plante sunt acestea?', 'epifite', 'ipefite', 'apofite', 'opifite', 'A', 'Medium');

INSERT INTO intrebari (intrebare, varianta_A, varianta_B, varianta_C, varianta_D, varianta_corecta, dificultate) VALUES
('Bareria de coral este un mediu marin deosebit, constituit, de fapt, din numeroase animale mici. Despre ce animale este vorba?', 'polipi', 'caracatite', 'crabi pustnici', 'melci', 'A', 'Hard'),
('Cascada Angel este cea mai inalta din lume. Unde trebuie sa mergem ca s-o putem vedea?', 'in Statele Unite ale Americii', 'in Venezuela', 'in Noua Zeelanda', 'in Groenlanda', 'B', 'Hard'),
('Mercurul este un metal lichid la temperatura ambianta. Din ce mineral se extrage de obicei?', 'pirita', 'cinabru', 'cuart', 'galena', 'B', 'Hard'),
('Odinioara, Pamantul avea o infatisare diferita de cea actuala: tot uscatul a fost candva unit intr-un singur supercontinent. Cum se numea acesta?', 'Pangeea', 'Panthalassa', 'Laurasia', 'Gondwana', 'A', 'Hard'),
('Ghepardul atinge o viteza de 120 kilometri pe ora si este cel mai rapid animal terestru. In preriile americane traieste un alt animal foarte iute, care poate alerga cu aproape 90 de kilometri pe ora. Cum se numeste?', 'antilocapra', 'mustang', 'ras rosu', 'coiot', 'A', 'Hard');
