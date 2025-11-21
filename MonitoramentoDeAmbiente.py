# ============================================================
# Monitoramento de Ambiente – Colab
# 3 gráficos (temperatura, umidade, ruído) com média
# Mostra APENAS dados recentes (janela de tempo configurável)
# ============================================================

!pip install plotly requests pytz numpy > /dev/null

from datetime import datetime, timedelta
import requests
import pytz
import numpy as np
import plotly.graph_objs as go
from plotly.subplots import make_subplots

# ===================== CONFIGURAÇÃO =====================

# >>> TROQUE PELO IP DA SUA VM FIWARE <<<
IP_ADDRESS = "102.37.18.193"
PORT_STH = 8666

ENTITY_ID = "urn:ngsi-ld:Env:001"
ENTITY_TYPE = "Environment"

FIWARE_SERVICE = "smart"
FIWARE_SERVICEPATH = "/"

# Últimas N medições que vamos buscar no STH
LAST_N_RECORDS = 100  # buscamos mais, mas vamos filtrar pela janela de tempo

# Janela de tempo "recente" (em horas) a partir do último dado
HOURS_WINDOW = 6  # ex.: últimos 6h; pode mudar para 1, 2, 12, 24...

# Cores pedidas:
COLOR_TEMP  = "#ff4d4d"  # vermelho
COLOR_HUM   = "#3b82f6"  # azul
COLOR_NOISE = "#a855f7"  # roxo
COLOR_MEAN  = "#d1d5db"  # cinza claro (média)

BG_PAPER = "#020617"
BG_PLOT  = "#0f172a"
GRID     = "rgba(148,163,184,0.25)"
TEXT     = "#e5e7eb"


# ===================== FUNÇÕES AUXILIARES =====================

def get_historical(attribute: str, lastN: int):
    """Busca histórico no STH-Comet para um atributo."""
    url = (
        f"http://{IP_ADDRESS}:{PORT_STH}/STH/v1/contextEntities/"
        f"type/{ENTITY_TYPE}/id/{ENTITY_ID}/attributes/{attribute}?lastN={lastN}"
    )
    headers = {
        "fiware-service": FIWARE_SERVICE,
        "fiware-servicepath": FIWARE_SERVICEPATH,
    }
    try:
        resp = requests.get(url, headers=headers, timeout=20)
        resp.raise_for_status()
        data = resp.json()
        values = data["contextResponses"][0]["contextElement"]["attributes"][0]["values"]
        return values
    except Exception as e:
        print(f"Erro ao buscar dados do STH-Comet ({attribute}):", e)
        return []


def convert_ts_and_values(raw_list):
    """Converte recvTime -> datetime (São Paulo) e extrai valores numéricos."""
    utc = pytz.utc
    sp = pytz.timezone("America/Sao_Paulo")

    times = []
    vals = []

    for item in raw_list:
        try:
            t = item["recvTime"].replace("T", " ").replace("Z", "")
            try:
                dt = datetime.strptime(t, "%Y-%m-%d %H:%M:%S.%f")
            except ValueError:
                dt = datetime.strptime(t, "%Y-%m-%d %H:%M:%S")
            dt_sp = utc.localize(dt).astimezone(sp)
            v = float(item["attrValue"])
            times.append(dt_sp)
            vals.append(v)
        except Exception as e:
            print("Erro convertendo dado:", e)

    # Ordena pelos timestamps
    pairs = sorted(zip(times, vals), key=lambda x: x[0])
    if not pairs:
        return [], []

    times_sorted = [p[0] for p in pairs]
    vals_sorted = [p[1] for p in pairs]
    return times_sorted, vals_sorted


def filtra_recentes(times, vals, hours_window: float):
    """Mantém apenas os pontos dentro da janela de horas em relação ao último timestamp."""
    if not times:
        return [], []

    max_ts = max(times)
    limite = max_ts - timedelta(hours=hours_window)

    times_new = []
    vals_new = []
    for t, v in zip(times, vals):
        if t >= limite:
            times_new.append(t)
            vals_new.append(v)

    return times_new, vals_new


def adiciona_traces(fig, row, times, vals, color_line, nome, unidade):
    """Adiciona linha + média em uma linha do subplot."""
    if not times:
        return

    mean_val = float(np.mean(vals))

    fig.add_trace(
        go.Scatter(
            x=times,
            y=vals,
            mode="lines+markers",
            name=nome,
            line=dict(color=color_line, width=3),
            marker=dict(size=8, color=color_line),
            hovertemplate=f"{nome}: "+"%{y:.2f} "+unidade+"<br>%{x}<extra></extra>",
        ),
        row=row, col=1
    )

    fig.add_trace(
        go.Scatter(
            x=times,
            y=[mean_val] * len(times),
            mode="lines",
            name=f"Média {nome} ({mean_val:.2f} {unidade})",
            line=dict(color=COLOR_MEAN, width=2, dash="dash"),
            hovertemplate=f"Média: {mean_val:.2f} {unidade}<extra></extra>",
        ),
        row=row, col=1
    )


# ===================== BUSCA E PREPARA OS DADOS =====================

temp_raw  = get_historical("temperature", LAST_N_RECORDS)
hum_raw   = get_historical("humidity", LAST_N_RECORDS)
noise_raw = get_historical("noiseLevel", LAST_N_RECORDS)

t_ts, t_vals = convert_ts_and_values(temp_raw)
h_ts, h_vals = convert_ts_and_values(hum_raw)
n_ts, n_vals = convert_ts_and_values(noise_raw)

# Filtra APENAS dados recentes (janela de HOURS_WINDOW horas)
t_ts, t_vals = filtra_recentes(t_ts, t_vals, HOURS_WINDOW)
h_ts, h_vals = filtra_recentes(h_ts, h_vals, HOURS_WINDOW)
n_ts, n_vals = filtra_recentes(n_ts, n_vals, HOURS_WINDOW)

print(f"Temperatura: {len(t_vals)} pontos após filtro")
print(f"Umidade    : {len(h_vals)} pontos após filtro")
print(f"Ruído      : {len(n_vals)} pontos após filtro")

# ===================== MONTA FIGURA COM 3 GRÁFICOS =====================

fig = make_subplots(
    rows=3, cols=1,
    shared_xaxes=True,
    vertical_spacing=0.07,
    subplot_titles=("Temperatura (°C)", "Umidade (%)", "Nível de Ruído (dB)")
)

adiciona_traces(fig, row=1, times=t_ts, vals=t_vals, color_line=COLOR_TEMP,  nome="Temperatura", unidade="°C")
adiciona_traces(fig, row=2, times=h_ts, vals=h_vals, color_line=COLOR_HUM,   nome="Umidade",    unidade="%")
adiciona_traces(fig, row=3, times=n_ts, vals=n_vals, color_line=COLOR_NOISE, nome="Ruído",      unidade="dB")

fig.update_layout(
    height=900,
    plot_bgcolor=BG_PLOT,
    paper_bgcolor=BG_PAPER,
    font=dict(color=TEXT, family="system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"),
    title=dict(
        text=f"Monitoramento de Ambiente – últimas medições recentes (janela de {HOURS_WINDOW}h)",
        x=0.5,
        font=dict(size=20, color=TEXT),
    ),
    legend=dict(
        orientation="v",
        x=1.02, y=1,
        bordercolor=TEXT,
        borderwidth=1,
        bgcolor="rgba(15,23,42,0.9)",
        font=dict(size=11),
    )
)

for i in range(1, 4):
    fig.update_yaxes(
        showgrid=True,
        gridcolor=GRID,
        zeroline=False,
        row=i, col=1
    )
    fig.update_xaxes(
        showgrid=True,
        gridcolor=GRID,
        zeroline=False,
        tickformat="%d/%m %H:%M",
        row=i, col=1
    )

fig.show()
