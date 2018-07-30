# Chocadeira
Controladora de Chocadeira sob NodeMCU - ESP12E e plataforma Arduino IDE.

# Como funciona

Verifica a temperatura, por meio do sensor DHT12, a fim de alterar o estado de um relé para aquecimento ou resfriamento.

Verifica a umidade, também por meio do sensor DHT12, a fim de informar inadequação por sinal sonoro (buzzer).

Conecta-se ao Wi-FI, por meio da biblioteca [WifiManager](https://github.com/tzapu/WiFiManager), e uma vez feito isso, conecta-se a um broker MQTT para comunicação com uma aplicação móvel dispensável [Chocadeira APP](https://github.com/DaviMagalhaes/ChocadeiraAPP).