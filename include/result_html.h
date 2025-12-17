String getResultPage(const String& deviceId, const String& apiResult) {
    if (apiResult == "MAC_EXISTS") {
        return R"rawliteral(
        <!DOCTYPE html>
        <html lang="uk">
        <head><meta charset="UTF-8"><title>SafePoint</title></head>
        <body>
        <script>
            alert("Цей пристрій вже зареєстрований!");
            window.location.href = "/";
        </script>
        </body>
        </html>
        )rawliteral";
    }

    // Повноцінна HTML сторінка для успішної реєстрації
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<title>SafePoint — Результат</title>
<style>
body { font-family: 'Segoe UI'; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; background:#e9f5f3;}
.card { background:#445f3b; color:#fff; padding:30px; border-radius:20px; width:360px; text-align:center; }
#deviceIdBox { background:#f5fff9; color:#000; padding:12px; border-radius:8px; font-family: monospace; margin:15px 0; word-break: break-all; font-size:16px; }
button { background:#3a5f47; color:#fff; border:none; border-radius:8px; padding:10px 20px; cursor:pointer; font-size:15px; margin:5px; }
button:hover { background:#284534; }
</style>
<script>
function copyId() {
  const id = document.getElementById("deviceIdBox").innerText;
  navigator.clipboard.writeText(id).then(()=>{alert("ID скопійовано!");});
}
</script>
</head>
<body>
<div class="card">
<h2>Пристрій зареєстровано</h2>
<div id="deviceIdBox">%DEVICE_ID%</div>
<button onclick="copyId()">Скопіювати ID</button>
</div>
</body>
</html>
)rawliteral";

    html.replace("%DEVICE_ID%", deviceId);
    return html;
}
