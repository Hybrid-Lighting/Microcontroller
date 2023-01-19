# Microcontroller
In deze repository staat de code die op de microcontroller wordt gedraaid.
Hierbij wordt gebruik gemaakt van de TI SimpleLink Wi-Fi CC3220S.
Door de microcontroller op de rest van het systeem aan te sluiten, kan er worden geschakeld tussen de accu en netstroom en zal er automatisch worden geschakeld naar netstroom, als het accupercentage onder de 20% is.
De geschreven code hier is gebaseerd op het voorbeeld "mqtt_client_CC3220S_LAUNCHXL_tirtos_ccs" wat te vinden is in de SDK van TI.
De hardware van de microcontroller wordt geconfigureerd in het bestand "mqtt_client.syscfg"
En de software wordt vooral geconfigureerd in "mqtt_client_app.c"
De andere bestanden zijn vooral drivers en interfaces die als ondersteuning dienen.
In README.html is de originele readme van de voorbeeld code te vinden.


## Installatie
Voor de development van de code wordt CCS (Code Composer Studio) van TI gebruikt.
Voor het aflezen van debug logs moet een seriele port sessie geopend worden, hiervoor is TeraTerm gebruikt.

### CCS
1. Download de laatste versie van CCS van https://software-dl.ti.com/ccs/esd/documents/ccs_downloads.html
2. Start de installatie
3. Kies verderop in de installatie voor Custom Installation
4. Zorg dat SimpleLink™ Wi-Fi® CC32xx Wireless MCUs geselecteerd is
5. Rond de installatie verder af.
6. Open CCS en maak een workspace aan.
7. Importeer deze repository als project in de workspace.
8. Mogelijk is het nog nodig om het rtos van ti wat gebruikt wordt (tirtos) te importen vanuit het sdk via de resource explorer.

### TeraTerm
TeraTerm kan gedownload worden vanaf https://osdn.net/projects/ttssh2/releases/


## Gebruik
Om de microcontroller met de rest van het systeem te gebruiken moeten de juiste pinnen aangesloten worden, die zijn bij Hardware te vinden.
De software kan gestart worden door de microcontroller te flashen, zodat de software opstart zodra de microcontroller aangaat, of door een debug sessie te starten.
De microcontroller verbindt met een WiFi netwerk om verbinding te maken met de MQTT server.
Vul de SSID en het wachtwoord van het te gebruiken netwerk in op regel 101 en 102 van "wifi_settings.h"

Als de microcontroller aanstaat, is door middel van de LEDs te zien welke stroombron gebruikt wordt voor het systeem.
Als de gele LED aanstaat, dan wordt de netstroom gebruikt en zullen ook P03 en P61 een hoog signaal geven om dit aan het systeem door te geven.
Als de rode LED aanstaat, dan wordt de accu gebruikt en zullen P03 en P61 een laag signaal geven.
De stroombron kan gewisseld worden door iets te publishen in het kanaal "HybridLighting/Power/Toggle" of door SW2 op de microcontroller in te drukken.

### Hardware
1. Sluit een Gnd pin aan op de Ground van het systeem
2. Sluit pin P59 aan op dc-adc
3. Sluit pin P03 aan op SD1
4. Sluit pin P61 aan op SD2
5. Sluit de microcontroller met de USB kabel aan op een externe voeding of een laptop voor een debug mogelijkheid

### Flashen
1. Build het project voor MCU+image in Code Composer Studio
2. Sluit de microcontroller aan via USB
3. Flash het project via Code Composer Studio

### Debug
1. Build het project voor Debug in Code Composer Studio
2. Debug het project
3. Resume de Debug sessie zodat de code start
4. Open Tera Term op de goede COM poort met een baudrate van 115200 om de debug logs te zien