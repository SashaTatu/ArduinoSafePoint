#pragma once

const char index_html[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<title>SafePoint — Wi-Fi Налаштування</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
body {
  font-family: 'Segoe UI', Arial, sans-serif;
  background-color: #e9f5f3;
  color: #fff;
  display: flex;
  justify-content: center;
  align-items: center;
  height: 100vh;
  margin: 0;
}
.card {
  background-color: #445f3b;
  color: #fff;
  padding: 30px;
  border-radius: 20px;
  width: 90%;
  max-width: 360px;
  text-align: center;
  box-shadow: 0 10px 25px rgba(0,0,0,0.3);
}
h2 {
  margin-top: 0;
  font-size: 1.6em;
  color: #eafaf0;
}
input {
  width: 100%;
  padding: 12px;
  margin: 12px 0;
  border: none;
  border-radius: 8px;
  background: #f5fff9;
  font-size: 16px;
  box-sizing: border-box;
}
button {
  background-color: #3a5f47;
  color: #fff;
  border: none;
  border-radius: 8px;
  padding: 12px;
  cursor: pointer;
  font-size: 16px;
  width: 100%;
  margin-top: 10px;
  transition: background 0.2s;
}
button:hover {
  background-color: #284534;
}
p {
  font-size: 14px;
  color: #cde5d6;
  margin-top: 15px;
}
</style>
</head>
<body>
<div class="card">
<h2>Підключення до Wi-Fi</h2>
<form action="/connect" method="post">
  <input type="text" name="ssid" placeholder="Назва мережі (SSID)" required>
  <input type="password" name="password" placeholder="Пароль Wi-Fi" required>
  <button type="submit">Підключити</button>
</form>
<p>Введіть дані вашої Wi-Fi мережі, на які буде працювати пристрій</p>
</div>
</body>
</html>
)rawliteral";
