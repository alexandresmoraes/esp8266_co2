// Variáveis globais
let co2Chart, co2HistoryChart, solenoidHistoryChart;
let co2Value = 0;
let tempValue = 0;
let solenoidActive = false;
let minCO2 = 800;
let maxCO2 = 1500;
let autoMode = true;

// Histórico de leituras
const co2History = [];
const tempHistory = [];

// Função principal de inicialização
document.addEventListener('DOMContentLoaded', function() {
    // Verificar se Plotly foi carregado
    if (typeof Plotly === 'undefined') {
        console.error('Erro: Plotly não está carregado. Verifique se plotly-basic.min.js foi incluído corretamente.');
        // Adiciona um fallback simples
        window.Plotly = {
            newPlot: function() { console.log('Plotly não carregado - newPlot chamado'); },
            update: function() { console.log('Plotly não carregado - update chamado'); },
            react: function() { console.log('Plotly não carregado - react chamado'); }
        };
    }
    
    // Inicializa as abas
    initTabs();
    
    // Inicializa os gráficos
    initCharts();
    
    // Adiciona eventos aos controles
    setupEventListeners();
    
    // Carrega dados iniciais
    fetchData();
    
    // Atualiza dados periodicamente (a cada 5 segundos)
    setInterval(fetchData, 5000);
    
    // Carrega o histórico inicialmente
    fetchHistory();
    
    // Carrega as configurações atuais
    loadConfig();
});

// Inicialização das abas
function initTabs() {
    document.querySelectorAll('.tab-btn').forEach(button => {
        button.addEventListener('click', () => {
            document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
            
            button.classList.add('active');
            document.getElementById(button.dataset.tab).classList.add('active');
            
            // Redimensiona os gráficos quando a aba é alterada
            setTimeout(function() {
                if (button.dataset.tab === 'history') {
                    window.dispatchEvent(new Event('resize'));
                } else if (button.dataset.tab === 'dashboard') {
                    window.dispatchEvent(new Event('resize'));
                }
            }, 100);
        });
    });
}

// Inicialização dos gráficos usando Plotly
function initCharts() {
    try {
        // Garantir que temos arrays vazios predefinidos
        const timeLabels = Array(20).fill().map((_, i) => `${20 - i}min`);
        const emptyData = Array(20).fill(null);
        
        // Gráfico de leituras recentes
        const recentData = [
            {
                x: timeLabels.slice(), // Clone do array para evitar referências compartilhadas
                y: emptyData.slice(),
                name: 'CO2 (ppm)',
                type: 'scatter',
                mode: 'lines',
                line: { color: '#2ecc71', shape: 'spline' },
                fill: 'tozeroy'
            },
            {
                x: timeLabels.slice(), // Clone do array para evitar referências compartilhadas
                y: emptyData.slice(),
                name: 'Temperatura (°C)',
                type: 'scatter',
                mode: 'lines',
                line: { color: '#e74c3c', shape: 'spline' },
                yaxis: 'y2'
            }
        ];
        
        const recentLayout = {
            autosize: true,
            margin: { l: 50, r: 50, t: 30, b: 50 },
            showlegend: true,
            legend: { orientation: 'h', y: 1.1 },
            xaxis: { title: 'Tempo' },
            yaxis: { 
                title: 'CO2 (ppm)',
                range: [400, 2000]
            },
            yaxis2: {
                title: 'Temperatura (°C)',
                overlaying: 'y',
                side: 'right',
                range: [10, 40]
            }
        };
        
        const recentConfig = {
            responsive: true,
            displayModeBar: false
        };
        
        co2Chart = Plotly.newPlot('readings-chart', recentData, recentLayout, recentConfig);
        
        // Gráfico de histórico de CO2
        const historyLabels = Array(24).fill().map((_, i) => `${23 - i}h`);
        
        const historyData = [
            {
                x: historyLabels.slice(),
                y: Array(24).fill(null),
                name: 'CO2 (ppm)',
                type: 'scatter',
                line: { color: '#2ecc71', shape: 'spline' },
                fill: 'tozeroy'
            }
        ];
        
        const historyLayout = {
            autosize: true,
            margin: { l: 50, r: 30, t: 30, b: 50 },
            showlegend: false,
            xaxis: { title: 'Tempo' },
            yaxis: { 
                title: 'CO2 (ppm)',
                range: [400, 2000]
            }
        };
        
        co2HistoryChart = Plotly.newPlot('co2-history-chart', historyData, historyLayout, recentConfig);
        
        // Gráfico de ativações do solenóide
        const solenoidLabels = Array(12).fill().map((_, i) => `${11 - i}h`);
        
        const solenoidData = [
            {
                x: solenoidLabels.slice(),
                y: Array(12).fill(null),
                name: 'Tempo Ativo (min)',
                type: 'bar',
                marker: { color: '#3498db' }
            }
        ];
        
        const solenoidLayout = {
            autosize: true,
            margin: { l: 50, r: 30, t: 30, b: 50 },
            showlegend: false,
            xaxis: { title: 'Tempo' },
            yaxis: { title: 'Tempo Ativo (min)' }
        };
        
        solenoidHistoryChart = Plotly.newPlot('solenoid-history-chart', solenoidData, solenoidLayout, recentConfig);
        
        console.log("Gráficos inicializados com sucesso");
    } catch (error) {
        console.error("Erro ao inicializar gráficos:", error);
    }
}

// Configuração dos event listeners
function setupEventListeners() {
    // Controle manual do solenóide
    const toggleBtn = document.getElementById('toggle-solenoid');
    if (toggleBtn) {
        toggleBtn.addEventListener('click', () => {
            toggleSolenoid();
        });
    }
    
    // Configuração dos sliders
    const minCO2Slider = document.getElementById('min-co2-slider');
    const minCO2Input = document.getElementById('min-co2');
    const maxCO2Slider = document.getElementById('max-co2-slider');
    const maxCO2Input = document.getElementById('max-co2');
    
    if (minCO2Slider && minCO2Input) {
        minCO2Slider.addEventListener('input', () => {
            minCO2Input.value = minCO2Slider.value;
        });
        
        minCO2Input.addEventListener('input', () => {
            minCO2Slider.value = minCO2Input.value;
        });
    }
    
    if (maxCO2Slider && maxCO2Input) {
        maxCO2Slider.addEventListener('input', () => {
            maxCO2Input.value = maxCO2Slider.value;
        });
        
        maxCO2Input.addEventListener('input', () => {
            maxCO2Slider.value = maxCO2Input.value;
        });
    }
    
    // Toggle do campo PIN baseado na seleção do modo de emparelhamento
    const bluetoothPairing = document.getElementById('bluetooth-pairing');
    if (bluetoothPairing) {
        bluetoothPairing.addEventListener('change', function() {
            const pinGroup = document.getElementById('pin-group');
            if (pinGroup) {
                if (this.value === 'pin') {
                    pinGroup.style.display = 'block';
                } else {
                    pinGroup.style.display = 'none';
                }
            }
        });
    }
    
    // Emparelhar com o dispositivo Smart Life Hub
    const pairDevice = document.getElementById('pair-device');
    if (pairDevice) {
        pairDevice.addEventListener('click', () => {
            const integration = document.getElementById('smart-integration');
            if (integration && integration.value === 'none') {
                showToast('Selecione primeiro o tipo de integração Smart Life');
                return;
            }
            
            showToast('Iniciando processo de emparelhamento com o Hub Smart Life...');
            
            fetch('/api/pair', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    smartlife_id: document.getElementById('smartlife-id')?.value || '',
                    pairing_mode: document.getElementById('bluetooth-pairing')?.value || 'auto',
                    pairing_pin: document.getElementById('pairing-pin')?.value || ''
                })
            })
            .then(response => response.json())
            .then(data => {
                if (data.status === 'success') {
                    showToast('Dispositivo emparelhado com sucesso!');
                } else {
                    showToast('Erro ao emparelhar: ' + data.message);
                }
            })
            .catch(error => {
                showToast('Erro de comunicação: ' + error);
            });
        });
    }
    
    // Salvar configurações
    const saveConfig = document.getElementById('save-config');
    if (saveConfig) {
        saveConfig.addEventListener('click', () => {
            saveConfigData();
        });
    }
    
    // Calibrar sensor
    const calibrateSensorBtn = document.getElementById('calibrate-sensor');
    if (calibrateSensorBtn) {
        calibrateSensorBtn.addEventListener('click', () => {
            if (confirm('Deseja calibrar o sensor MH-Z19E? Certifique-se de que o sensor está em ambiente com ar fresco.')) {
                calibrateSensor();
            }
        });
    }
}

// Função para buscar dados atuais
function fetchData() {
    fetch('/api/data')
        .then(response => {
            if (!response.ok) {
                throw new Error(`Erro na resposta da API: ${response.status}`);
            }
            return response.json();
        })
        .then(data => {
            if (data) {
                updateUI(data);
            } else {
                console.error('Dados vazios recebidos da API');
            }
        })
        .catch(error => {
            console.error('Erro ao buscar dados:', error);
        });
}

// Função para buscar histórico
function fetchHistory() {
    fetch('/api/history')
        .then(response => {
            if (!response.ok) {
                throw new Error(`Erro na resposta da API: ${response.status}`);
            }
            return response.json();
        })
        .then(data => {
            if (data) {
                updateHistory(data);
            } else {
                console.error('Dados vazios recebidos da API de histórico');
            }
        })
        .catch(error => {
            console.error('Erro ao buscar histórico:', error);
        });
}

// Atualiza a interface com os dados recebidos
function updateUI(data) {
    try {
        // Atualiza valores
        co2Value = data.co2 || 0;
        tempValue = data.temperature || 0;
        solenoidActive = !!data.solenoid;
        minCO2 = data.min_co2 || 800;
        maxCO2 = data.max_co2 || 1500;
        autoMode = !!data.auto_mode;
        
        // Atualiza exibição dos valores
        const co2ValueElement = document.getElementById('co2-value');
        if (co2ValueElement) co2ValueElement.textContent = co2Value;
        
        const tempValueElement = document.getElementById('temp-value');
        if (tempValueElement) tempValueElement.textContent = tempValue.toFixed(1);
        
        // Atualiza gauge de CO2
        const co2Gauge = document.getElementById('co2-gauge');
        if (co2Gauge) {
            const gaugeHeight = Math.min(100, (co2Value / 2000) * 100);
            co2Gauge.style.height = `${gaugeHeight}%`;
        }
        
        // Atualiza status do CO2
        const co2Status = document.getElementById('co2-status');
        if (co2Status) {
            if (co2Value < minCO2) {
                co2Status.textContent = 'BAIXO';
                co2Status.style.color = '#e74c3c';
            } else if (co2Value > maxCO2) {
                co2Status.textContent = 'ALTO';
                co2Status.style.color = '#e74c3c';
            } else {
                co2Status.textContent = 'IDEAL';
                co2Status.style.color = '#2ecc71';
            }
        }
        
        // Atualiza indicador de temperatura
        const tempIndicator = document.getElementById('temp-indicator');
        const tempStatus = document.getElementById('temp-status');
        
        if (tempIndicator && tempStatus) {
            if (tempValue < 15) {
                tempIndicator.style.backgroundColor = '#3498db';
                tempStatus.textContent = 'Baixa';
            } else if (tempValue > 30) {
                tempIndicator.style.backgroundColor = '#e74c3c';
                tempStatus.textContent = 'Alta';
            } else {
                tempIndicator.style.backgroundColor = '#f39c12';
                tempStatus.textContent = 'Normal';
            }
        }
        
        // Atualiza status do solenóide
        updateSolenoidUI();
        
        // Atualiza o status do modo
        const toggleSolenoidBtn = document.getElementById('toggle-solenoid');
        if (toggleSolenoidBtn) {
            toggleSolenoidBtn.disabled = autoMode;
            toggleSolenoidBtn.textContent = autoMode ? 'Modo Automático Ativo' : 'Controle Manual';
        }
        
        // Atualiza status do WiFi
        const wifiName = document.getElementById('wifi-name');
        if (wifiName) {
            wifiName.textContent = data.wifi_mode === 'client' 
                ? `Conectado a: ${data.wifi_ssid} (${convertRSSIToPercent(data.wifi_strength)}%)` 
                : 'Modo AP: ESP8266_CO2';
        }
        
        // Armazena os valores de histórico
        co2History.push(co2Value);
        if (co2History.length > 20) {
            co2History.shift();
        }
        
        tempHistory.push(tempValue);
        if (tempHistory.length > 20) {
            tempHistory.shift();
        }
        
        // Atualiza o gráfico com os novos dados
        updateCharts();
    } catch (error) {
        console.error('Erro ao atualizar UI:', error);
    }
}

// Atualiza os gráficos em tempo real
function updateCharts() {
    try {
        if (!co2History || !tempHistory) {
            console.error("Histórico de dados não está definido");
            return;
        }
        
        // Garantir que temos valores válidos
        const co2Data = [...co2History];
        const tempData = [...tempHistory];
        
        // Garante que temos 20 pontos para gráfico
        while (co2Data.length < 20) co2Data.unshift(null);
        while (tempData.length < 20) tempData.unshift(null);
        
        // Atualiza gráfico com os dados atuais
        try {
            Plotly.update('readings-chart', {
                y: [co2Data, tempData]
            });
            console.log("Gráfico atualizado com sucesso");
        } catch (error) {
            console.error("Erro ao atualizar o gráfico:", error);
        }
    } catch (error) {
        console.error("Erro em updateCharts:", error);
    }
}

// Atualiza os gráficos de histórico
function updateHistory(data) {
    try {
        if (!data || !data.co2 || !data.temp || !data.time) {
            console.warn("Dados de histórico inválidos ou incompletos");
            return;
        }
        
        // Formata os rótulos de tempo
        const historyTimeLabels = data.time.map(t => {
            if (t === 0) return 'Agora';
            if (t > -60) return `${Math.abs(t)}min`;
            return `${Math.round(Math.abs(t) / 60)}h`;
        });
        
        // Atualiza o gráfico de histórico de CO2
        try {
            Plotly.update('co2-history-chart', {
                x: [historyTimeLabels],
                y: [data.co2]
            });
            console.log("Gráfico de histórico de CO2 atualizado");
        } catch (error) {
            console.error("Erro ao atualizar o gráfico de histórico de CO2:", error);
        }
        
        // Gera dados para o gráfico de ativações do solenóide (exemplo)
        // Na implementação real, esses dados viriam do servidor
        const solenoidData = Array(12).fill().map(() => Math.floor(Math.random() * 30));
        const solenoidLabels = Array(12).fill().map((_, i) => `${11 - i}h`);
        
        try {
            Plotly.update('solenoid-history-chart', {
                x: [solenoidLabels],
                y: [solenoidData]
            });
            console.log("Gráfico de histórico do solenóide atualizado");
        } catch (error) {
            console.error("Erro ao atualizar o gráfico de histórico do solenóide:", error);
        }
    } catch (error) {
        console.error("Erro em updateHistory:", error);
    }
}

// Carrega as configurações atuais
function loadConfig() {
    fetch('/api/config')
        .then(response => {
            if (!response.ok) {
                throw new Error(`Erro na resposta da API: ${response.status}`);
            }
            return response.json();
        })
        .then(data => {
            if (!data) return;
            
            // Preenche o formulário com os valores recebidos
            const wifiSsid = document.getElementById('wifi-ssid');
            if (wifiSsid) wifiSsid.value = data.wifi_ssid || '';
            
            const wifiPassword = document.getElementById('wifi-password');
            if (wifiPassword) wifiPassword.value = ''; // Não exibe a senha por segurança
            
            const minCo2Input = document.getElementById('min-co2');
            const minCo2Slider = document.getElementById('min-co2-slider');
            if (minCo2Input) minCo2Input.value = data.min_co2;
            if (minCo2Slider) minCo2Slider.value = data.min_co2;
            
            const maxCo2Input = document.getElementById('max-co2');
            const maxCo2Slider = document.getElementById('max-co2-slider');
            if (maxCo2Input) maxCo2Input.value = data.max_co2;
            if (maxCo2Slider) maxCo2Slider.value = data.max_co2;
            
            const readingInterval = document.getElementById('reading-interval');
            if (readingInterval) readingInterval.value = data.reading_interval;
            
            const operationMode = document.getElementById('operation-mode');
            if (operationMode) operationMode.value = data.auto_mode.toString();
            
            // Configurações Smart Life
            if (data.smartlife_id) {
                const smartIntegration = document.getElementById('smart-integration');
                const smartLifeId = document.getElementById('smartlife-id');
                const smartLifeKey = document.getElementById('smartlife-key');
                const enableNotifications = document.getElementById('enable-notifications');
                
                if (smartIntegration) smartIntegration.value = 'smartlife';
                if (smartLifeId) smartLifeId.value = data.smartlife_id;
                if (smartLifeKey) smartLifeKey.value = data.smartlife_key || '';
                if (enableNotifications) enableNotifications.checked = data.enable_notifications;
            }
        })
        .catch(error => {
            console.error('Erro ao carregar configurações:', error);
        });
}

// Salva as configurações
function saveConfigData() {
    try {
        // Coletando os valores do formulário
        const wifiSsid = document.getElementById('wifi-ssid')?.value || '';
        const wifiPassword = document.getElementById('wifi-password')?.value || '';
        const minCo2 = parseInt(document.getElementById('min-co2')?.value || '800');
        const maxCo2 = parseInt(document.getElementById('max-co2')?.value || '1500');
        const readingInterval = parseInt(document.getElementById('reading-interval')?.value || '30');
        const operationMode = document.getElementById('operation-mode')?.value === 'true';
        const smartLifeId = document.getElementById('smartlife-id')?.value || '';
        const smartLifeKey = document.getElementById('smartlife-key')?.value || '';
        const enableNotifications = document.getElementById('enable-notifications')?.checked || false;
        
        const config = {
            wifi_ssid: wifiSsid,
            wifi_pass: wifiPassword,
            min_co2: minCo2,
            max_co2: maxCo2,
            reading_interval: readingInterval,
            operation_mode: operationMode,
            smartlife_id: smartLifeId,
            smartlife_key: smartLifeKey,
            enable_notifications: enableNotifications
        };
        
        // Validação básica
        if (config.min_co2 >= config.max_co2) {
            showToast('Erro: O nível mínimo de CO2 deve ser menor que o máximo!');
            return;
        }
        
        // Enviar para o servidor
        const saveButton = document.getElementById('save-config');
        if (saveButton) saveButton.classList.add('loading');
        
        fetch('/api/config', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(config)
        })
        .then(response => response.json())
        .then(data => {
            if (saveButton) saveButton.classList.remove('loading');
            
            if (data.status === 'success') {
                showToast(data.message);
                
                // Se as configurações de WiFi foram alteradas, o dispositivo pode reiniciar
                if (config.wifi_ssid) {
                    showToast('Reconectando ao dispositivo em 10 segundos...');
                    setTimeout(() => {
                        window.location.reload();
                    }, 10000);
                }
            } else {
                showToast('Erro: ' + data.message);
            }
        })
        .catch(error => {
            if (saveButton) saveButton.classList.remove('loading');
            showToast('Erro de comunicação: ' + error);
        });
    } catch (error) {
        console.error("Erro ao salvar configurações:", error);
        showToast('Erro ao processar configurações: ' + error.message);
    }
}

// Função para controle manual do solenóide
function toggleSolenoid() {
    if (autoMode) {
        showToast('O solenóide está em modo automático. Mude para modo manual nas configurações.');
        return;
    }
    
    fetch('/api/solenoid', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            state: !solenoidActive
        })
    })
    .then(response => response.json())
    .then(data => {
        if (data.status === 'success') {
            solenoidActive = data.state;
            updateSolenoidUI();
        } else {
            showToast('Erro: ' + data.message);
        }
    })
    .catch(error => {
        showToast('Erro de comunicação: ' + error);
    });
}

// Atualiza a interface do solenóide
function updateSolenoidUI() {
    const solenoidStatusElement = document.getElementById('solenoid-status');
    const solenoidDescElement = document.getElementById('solenoid-desc');
    
    if (solenoidStatusElement && solenoidDescElement) {
        if (solenoidActive) {
            solenoidStatusElement.className = 'status-indicator status-on';
            const statusText = solenoidStatusElement.querySelector('.status-text');
            if (statusText) statusText.textContent = 'ATIVO';
            solenoidDescElement.textContent = 'Liberando CO2';
        } else {
            solenoidStatusElement.className = 'status-indicator status-off';
            const statusText = solenoidStatusElement.querySelector('.status-text');
            if (statusText) statusText.textContent = 'INATIVO';
            solenoidDescElement.textContent = 'CO2 pausado';
        }
    }
}

// Função para calibrar o sensor
function calibrateSensor() {
    fetch('/api/calibrate', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        if (data.status === 'success') {
            showToast('Calibração iniciada! Isso pode levar alguns minutos.');
        } else {
            showToast('Erro: ' + data.message);
        }
    })
    .catch(error => {
        showToast('Erro de comunicação: ' + error);
    });
}

// Converte o valor RSSI (dBm) para porcentagem
function convertRSSIToPercent(rssi) {
    if (rssi === undefined || rssi === null) return 0;
    
    // Os valores de RSSI geralmente variam de -30 dBm (excelente) a -90 dBm (péssimo)
    const MIN_RSSI = -100;
    const MAX_RSSI = -30;
    
    // Limita os valores dentro do intervalo
    const limitedRSSI = Math.max(MIN_RSSI, Math.min(MAX_RSSI, rssi));
    
    // Calcula a porcentagem (invertida, pois RSSI são valores negativos)
    const percent = Math.round(100 - Math.abs(((limitedRSSI - MAX_RSSI) / (MIN_RSSI - MAX_RSSI)) * 100));
    
    return percent;
}

// Exibe uma mensagem toast
function showToast(message) {
    // Verifica se já existe um toast
    let toast = document.querySelector('.toast');
    
    if (!toast) {
        // Cria um novo elemento toast
        toast = document.createElement('div');
        toast.className = 'toast';
        document.body.appendChild(toast);
    }
    
    // Define a mensagem e mostra o toast
    toast.textContent = message;
    toast.classList.add('show');
    
    // Esconde o toast após 3 segundos
    setTimeout(() => {
        toast.classList.remove('show');
    }, 3000);
}