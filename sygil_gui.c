#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>


static short state = 0;

// checks tracerpid in /proc/self/status, this shit still works btw
static void chk(void) {
    char b[256] = {0};
    int f = open("/proc/self/status", 0);
    if (f < 0) return;
    read(f, b, 255);

    if (!strstr(b, "TracerPid:\t0")) {
        state = 1;
    }
    close(f);
}

// fnv1aish hash, nothing fancy. p1/p2 split just to make it look less obvious
static void gen(const char *in, char *out) {
    unsigned int h = 0x811c9dc5;

    while (*in && *in != '\n' && *in != '\r') {
        h ^= (unsigned char)*in++;
        h *= 0x01000193;
    }

    // if we're being traced, poison the result 
    if (state) {
        h ^= 0xDEADBEEF;
    }

    unsigned int p1 = h ^ 0x5947494C;
    unsigned int p2 = (h >> 16) ^ (h & 0xFFFF);

    sprintf(out, "syg-%08x-%04x-%08x", p1, p2, p1 ^ p2 ^ 0x535947);
}

static void handle_script_message(WebKitUserContentManager *manager, WebKitJavascriptResult *result, gpointer user_data) {
    (void)manager;

    JSCValue *value = webkit_javascript_result_get_js_value(result);
    if (!jsc_value_is_object(value)) return;

    // exit message from the fadeout on window close
    if (jsc_value_object_has_property(value, "action")) {
        char *action = jsc_value_to_string(jsc_value_object_get_property(value, "action"));
        if (strcmp(action, "exit") == 0) {
            g_free(action);
            exit(0);
        }
        g_free(action);
        return;
    }

    char *alias = jsc_value_to_string(jsc_value_object_get_property(value, "alias"));
    char *token = jsc_value_to_string(jsc_value_object_get_property(value, "token"));

    if (!alias || !token) {
        if (alias) g_free(alias);
        if (token) g_free(token);
        return;
    }

    WebKitWebView *web_view = WEBKIT_WEB_VIEW(user_data);

    if (strlen(alias) < 4) {
        webkit_web_view_evaluate_javascript(
            web_view,
            "triggerResponse(false, 'insufficient name length.');",
            (gssize)-1, NULL, NULL, NULL, NULL, NULL
        );
        g_free(alias);
        g_free(token);
        return;
    }

    char expected[64];
    gen(alias, expected);

    if (strcmp(token, expected) == 0) {
        webkit_web_view_evaluate_javascript(
            web_view,
            "triggerResponse(true, '');",
            (gssize)-1, NULL, NULL, NULL, NULL, NULL
        );
    } else {
        webkit_web_view_evaluate_javascript(
            web_view,
            "triggerResponse(false, 'the spirit rejects.');",
            (gssize)-1, NULL, NULL, NULL, NULL, NULL
        );
    }

    g_free(alias);
    g_free(token);
}

// card fade thingamajig
static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(user_data);

    webkit_web_view_evaluate_javascript(
        web_view,
        "const card = document.getElementById('main-card');"
        "card.style.transition = 'all 0.5s ease-in';"
        "card.style.opacity = '0';"
        "setTimeout(() => { window.webkit.messageHandlers.sygilBridge.postMessage({action: 'exit'}); }, 500);",
        (gssize)-1, NULL, NULL, NULL, NULL, NULL
    );

    return TRUE;
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    chk();

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 680);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

    GtkWidget *header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "sygil.fun // made with <3 by lovemf");
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

    // headerbar theming, keep it dark, matches the webview
    GtkCssProvider *css_provider = gtk_css_provider_new();
    const char *gtk_css =
        "@keyframes pulse-title {\n"
        "  0% { text-shadow: 0 0 4px rgba(255,255,255,0.2); }\n"
        "  100% { text-shadow: 0 0 10px rgba(255,255,255,0.7), 0 0 15px rgba(255,255,255,0.4); }\n"
        "}\n"
        "headerbar { background: #000000; border: none; box-shadow: inset 0 -1px rgba(255,255,255,0.05); }\n"
        "headerbar label {\n"
        "  color: #ffffff;\n"
        "  font-family: monospace;\n"
        "  font-size: 13px;\n"
        "  letter-spacing: 0.5px;\n"
        "  animation: pulse-title 3s infinite alternate ease-in-out;\n"
        "}\n"
        "button.titlebutton {\n"
        "  color: #555555;\n"
        "  background: transparent;\n"
        "  border: none;\n"
        "  box-shadow: none;\n"
        "  transition: all 0.3s ease;\n"
        "}\n"
        "button.titlebutton:hover {\n"
        "  color: #ffffff;\n"
        "  background: transparent;\n"
        "  text-shadow: 0 0 8px #ffffff, 0 0 15px rgba(255,255,255,0.8);\n"
        "}\n";
    gtk_css_provider_load_from_data(css_provider, gtk_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    WebKitUserContentManager *content_manager = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(content_manager, "sygilBridge");

    GtkWidget *web_view = webkit_web_view_new_with_user_content_manager(content_manager);
    GdkRGBA rgba = {0.0, 0.0, 0.0, 1.0};
    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(web_view), &rgba);

    g_signal_connect(content_manager, "script-message-received::sygilBridge", G_CALLBACK(handle_script_message), web_view);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_window_delete), web_view);

    gtk_container_add(GTK_CONTAINER(window), web_view);

    // the actual ui, lives entirely in the webview since gtk widgets look like ass
    const char *html_content =
        "<!DOCTYPE html>\n"
        "<html lang='en'>\n"
        "<head>\n"
        "<meta charset='UTF-8'>\n"
        "<title>Sygil Node</title>\n"
        "<style>\n"
        "  @import url('https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@300;400;500;700&display=swap');\n"
        "  \n"
        "  :root {\n"
        "    --bg-base: #000000;\n"
        "    --frame-glow: rgba(255, 255, 255, 0.4);\n"
        "  }\n"
        "  \n"
        "  * {\n"
        "    box-sizing: border-box;\n"
        "    margin: 0;\n"
        "    padding: 0;\n"
        "    font-family: 'JetBrains Mono', monospace;\n"
        "    -webkit-user-select: none;\n"
        "    user-select: none;\n"
        "  }\n"
        "  \n"
        "  body {\n"
        "    background-color: var(--bg-base);\n"
        "    color: #ffffff;\n"
        "    height: 100vh;\n"
        "    display: flex;\n"
        "    justify-content: center;\n"
        "    align-items: center;\n"
        "    overflow: hidden;\n"
        "    position: relative;\n"
        "  }\n"
        "  \n"
        "  .terminal-frame {\n"
        "    position: relative;\n"
        "    z-index: 10;\n"
        "    width: 440px;\n"
        "    background-color: rgba(0, 0, 0, 0.98);\n"
        "    border: 1px solid var(--frame-glow);\n"
        "    border-radius: 4px;\n"
        "    box-shadow: 0 0 25px rgba(255, 255, 255, 0.15), inset 0 0 15px rgba(255,255,255,0.08);\n"
        "    display: flex;\n"
        "    flex-direction: column;\n"
        "    opacity: 0;\n"
        "    animation: boot-up 0.8s cubic-bezier(0.2, 0.8, 0.2, 1) forwards;\n"
        "    will-change: opacity, transform;\n"
        "  }\n"
        "  \n"
        "  @keyframes boot-up {\n"
        "    0% { opacity: 0; transform: scale(0.95); }\n"
        "    100% { opacity: 1; transform: scale(1); }\n"
        "  }\n"
        "  \n"
        "  @keyframes text-dissolve {\n"
        "    0% { opacity: 1; transform: translateY(0); }\n"
        "    100% { opacity: 0; transform: translateY(15px); pointer-events: none; }\n"
        "  }\n"
        "  @keyframes text-fade-in {\n"
        "    0% { opacity: 0; transform: translateY(-10px); }\n"
        "    100% { opacity: 1; transform: translateY(0); }\n"
        "  }\n"
        "  \n"
        "  @keyframes breathing-glow {\n"
        "    0% { text-shadow: 0 0 4px rgba(255,255,255,0.1); color: #777777; }\n"
        "    50% { text-shadow: 0 0 15px rgba(255,255,255,0.7), 0 0 20px rgba(255,255,255,0.4); color: #cccccc; }\n"
        "    100% { text-shadow: 0 0 4px rgba(255,255,255,0.1); color: #777777; }\n"
        "  }\n"
        "  .breathing-text {\n"
        "    font-size: 11px;\n"
        "    text-align: center;\n"
        "    margin-top: 25px;\n"
        "    font-style: italic;\n"
        "    line-height: 1.6;\n"
        "    animation: breathing-glow 4s infinite ease-in-out;\n"
        "  }\n"
        "  \n"
        "  .tf-header {\n"
        "    display: flex;\n"
        "    justify-content: space-between;\n"
        "    align-items: center;\n"
        "    padding: 12px 20px;\n"
        "    border-bottom: 1px solid rgba(255, 255, 255, 0.2);\n"
        "    background-color: rgba(255, 255, 255, 0.03);\n"
        "  }\n"
        "  .tf-header span.title {\n"
        "    font-size: 10px;\n"
        "    color: #aaaaaa;\n"
        "    letter-spacing: 0.5px;\n"
        "    font-style: italic;\n"
        "  }\n"
        "  \n"
        "  .insight-btn {\n"
        "    font-size: 10px;\n"
        "    color: #888888;\n"
        "    letter-spacing: 2px;\n"
        "    cursor: pointer;\n"
        "    transition: all 0.3s ease;\n"
        "  }\n"
        "  .insight-btn:hover {\n"
        "    color: #ffffff;\n"
        "    text-shadow: 0 0 10px #ffffff;\n"
        "  }\n"
        "  \n"
        "  .tf-body {\n"
        "    padding: 30px 40px;\n"
        "    display: flex;\n"
        "    flex-direction: column;\n"
        "    gap: 25px;\n"
        "  }\n"
        "  \n"
        "  .input-wrapper {\n"
        "    position: relative;\n"
        "    border-bottom: 1px solid rgba(255, 255, 255, 0.2);\n"
        "    transition: border-color 0.4s ease;\n"
        "  }\n"
        "  .input-wrapper:focus-within {\n"
        "    border-bottom: 1px solid rgba(255, 255, 255, 0.9);\n"
        "  }\n"
        "  .input-wrapper input {\n"
        "    width: 100%;\n"
        "    padding: 18px 4px 8px 4px;\n"
        "    background: transparent;\n"
        "    border: none;\n"
        "    color: #ffffff;\n"
        "    font-size: 14px;\n"
        "    outline: none;\n"
        "    -webkit-user-select: text;\n"
        "    user-select: text;\n"
        "    caret-color: transparent;\n"
        "  }\n"
        "  .input-wrapper label {\n"
        "    position: absolute;\n"
        "    top: 14px;\n"
        "    left: 4px;\n"
        "    color: #777777;\n"
        "    font-size: 13px;\n"
        "    pointer-events: none;\n"
        "    transition: transform 0.3s cubic-bezier(0.2, 0, 0, 1), color 0.3s;\n"
        "    transform-origin: left top;\n"
        "  }\n"
        "  .input-wrapper input:focus ~ label,\n"
        "  .input-wrapper input:not(:placeholder-shown) ~ label {\n"
        "    transform: translateY(-14px) scale(0.75);\n"
        "    color: #cccccc;\n"
        "  }\n"
        "  .input-wrapper input:focus ~ label { color: #ffffff; }\n"
        "  \n"
        "  .physics-caret-container {\n"
        "    position: absolute;\n"
        "    bottom: 8px;\n"
        "    left: 4px;\n"
        "    width: 1px;\n"
        "    height: 16px;\n"
        "    transition: transform 0.15s cubic-bezier(0.22, 1, 0.36, 1);\n"
        "    pointer-events: none;\n"
        "  }\n"
        "  .physics-caret-inner {\n"
        "    width: 100%;\n"
        "    height: 100%;\n"
        "    background-color: #ffffff;\n"
        "    opacity: 0;\n"
        "  }\n"
        "  .input-wrapper input:focus ~ .physics-caret-container .physics-caret-inner {\n"
        "    animation: caret-ghost 1.5s infinite ease-in-out;\n"
        "  }\n"
        "  @keyframes caret-ghost {\n"
        "    0%, 100% { opacity: 0.1; box-shadow: none; }\n"
        "    50% { opacity: 1; box-shadow: 0 0 8px #ffffff; }\n"
        "  }\n"
        "  \n"
        "  button {\n"
        "    width: 100%;\n"
        "    background: transparent;\n"
        "    color: #aaaaaa;\n"
        "    border: 1px solid rgba(255,255,255,0.3);\n"
        "    border-left: none;\n"
        "    border-right: none;\n"
        "    padding: 16px;\n"
        "    font-weight: 400;\n"
        "    font-size: 12px;\n"
        "    letter-spacing: 4px;\n"
        "    text-transform: uppercase;\n"
        "    cursor: pointer;\n"
        "    transition: all 0.3s ease;\n"
        "  }\n"
        "  button:hover {\n"
        "    color: #ffffff;\n"
        "    background: rgba(255,255,255,0.08);\n"
        "    text-shadow: 0 0 10px #ffffff;\n"
        "    letter-spacing: 5px;\n"
        "    border-color: rgba(255,255,255,0.8);\n"
        "  }\n"
        "  button:active { transform: scale(0.98); }\n"
        "  \n"
        "  .tf-readout {\n"
        "    height: 80px;\n"
        "    display: flex;\n"
        "    flex-direction: column;\n"
        "    justify-content: center;\n"
        "    align-items: center;\n"
        "    padding: 0 20px;\n"
        "  }\n"
        "  #status-text {\n"
        "    font-size: 11px;\n"
        "    color: #777777;\n"
        "    text-align: center;\n"
        "  }\n"
        "  .hint-text {\n"
        "    color: #bbbbbb !important;\n"
        "  }\n"
        "  \n"
        "  #prominent-success {\n"
        "    position: absolute;\n"
        "    top: 50%;\n"
        "    left: 50%;\n"
        "    transform: translate(-50%, -50%);\n"
        "    font-family: 'JetBrains Mono', monospace;\n"
        "    font-size: 16px;\n"
        "    letter-spacing: 12px;\n"
        "    color: #ffffff;\n"
        "    text-align: center;\n"
        "    white-space: nowrap;\n"
        "    text-shadow: 0 0 25px rgba(255, 255, 255, 1);\n"
        "    opacity: 0;\n"
        "    z-index: 100;\n"
        "    pointer-events: none;\n"
        "    transition: opacity 2.5s ease-in-out;\n"
        "  }\n"
        "  \n"
        "  .error-short {\n"
        "    color: #ff3b3b !important;\n"
        "    text-shadow: 0 0 8px rgba(255,0,0,0.6);\n"
        "    animation: smooth-shake 0.4s cubic-bezier(.36,.07,.19,.97) both;\n"
        "  }\n"
        "  @keyframes smooth-shake {\n"
        "    0%, 100% { transform: translateX(0); }\n"
        "    20%, 60% { transform: translateX(-4px); }\n"
        "    40%, 80% { transform: translateX(4px); }\n"
        "  }\n"
        "  .error-reject {\n"
        "    color: #ff3b3b !important;\n"
        "    animation: error-fade 0.5s ease-out forwards;\n"
        "  }\n"
        "  @keyframes error-fade {\n"
        "    0% { opacity: 0; transform: scale(0.95); }\n"
        "    100% { opacity: 1; transform: scale(1); text-shadow: 0 0 12px rgba(255,0,0,0.6); }\n"
        "  }\n"
        "  \n"
        "  #text-measurer {\n"
        "    position: absolute;\n"
        "    visibility: hidden;\n"
        "    font-size: 14px;\n"
        "    white-space: pre;\n"
        "  }\n"
        "  \n"
        "  #matrix-canvas {\n"
        "    position: absolute;\n"
        "    top: 0; left: 0; width: 100%; height: 100%;\n"
        "    z-index: 0;\n"
        "    pointer-events: none;\n"
        "  }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "\n"
        "  <canvas id='matrix-canvas'></canvas>\n"
        "  <div id='prominent-success'></div>\n"
        "\n"
        "  <div class='terminal-frame' id='main-card'>\n"
        "    <div class='tf-header'>\n"
        "       <span class='title'>sygil.fun // Proverbs 25:2</span>\n"
        "       <span class='insight-btn' onclick='cycleHint()'>[ insight ]</span>\n"
        "    </div>\n"
        "    <div class='tf-body'>\n"
        "      <div class='input-wrapper'>\n"
        "        <input type='text' id='alias' placeholder=' ' spellcheck='false' autocomplete='off'>\n"
        "        <label>true name</label>\n"
        "        <div class='physics-caret-container'><div class='physics-caret-inner'></div></div>\n"
        "      </div>\n"
        "      <div class='input-wrapper'>\n"
        "        <input type='text' id='token' placeholder=' ' spellcheck='false' autocomplete='off'>\n"
        "        <label>binding seal</label>\n"
        "        <div class='physics-caret-container'><div class='physics-caret-inner'></div></div>\n"
        "      </div>\n"
        "    </div>\n"
        "    <button onclick='executePact()'>authenticate</button>\n"
        "    <div class='tf-readout'>\n"
        "      <div id='status-text'>awaiting ritual input</div>\n"
        "    </div>\n"
        "  </div>\n"
        "\n"
        "  <span id='text-measurer'></span>\n"
        "\n"
        "  <script>\n"
        "    let isShuttingDown = false;\n"
        "    let uiLocked = false; \n"
        "\n"
        "    const measurer = document.getElementById('text-measurer');\n"
        "    function updateCaretPosition(inputElement) {\n"
        "      const wrapper = inputElement.parentElement;\n"
        "      const caretContainer = wrapper.querySelector('.physics-caret-container');\n"
        "      const textToCursor = inputElement.value.substring(0, inputElement.selectionStart);\n"
        "      if (textToCursor.length === 0) { caretContainer.style.transform = `translateX(0px)`; return; }\n"
        "      measurer.textContent = textToCursor.replace(/\\s/g, '\\u00a0');\n"
        "      caretContainer.style.transform = `translateX(${measurer.getBoundingClientRect().width}px)`;\n"
        "    }\n"
        "    function bindCaretEvents() {\n"
        "      document.querySelectorAll('.input-wrapper input').forEach(input => {\n"
        "        ['input', 'keyup', 'click', 'focus'].forEach(ev => input.addEventListener(ev, e => updateCaretPosition(e.target)));\n"
        "      });\n"
        "    }\n"
        "    bindCaretEvents();\n"
        "\n"
        "    // little breadcrumbs for whoever's poking at this in devtools\n"
        "    const hints = [\"[ i ] The watcher's PID alters the seed.\", \"[ ii ] 0x01000193 multiplies the XOR.\", \"[ iii ] High sixteen, low sixteen, folded.\"];\n"
        "    let hintIndex = 0;\n"
        "    function cycleHint() {\n"
        "      if(isShuttingDown || uiLocked) return;\n"
        "      uiLocked = true;\n"
        "      \n"
        "      if(window.decryptInterval) clearInterval(window.decryptInterval);\n"
        "      const st = document.getElementById('status-text');\n"
        "      st.className = ''; void st.offsetWidth;\n"
        "      st.className = 'hint-text';\n"
        "      \n"
        "      const targetText = hints[hintIndex];\n"
        "      hintIndex = (hintIndex + 1) % hints.length;\n"
        "      const glyphs = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*Δ∇⎊☿♄♅♆♇';\n"
        "      let iteration = 0;\n"
        "      window.decryptInterval = setInterval(() => {\n"
        "        st.innerText = targetText.split('').map((l, i) => { if(l===' '||l==='['||l===']') return l; return i < iteration ? l : glyphs[Math.floor(Math.random()*glyphs.length)]; }).join('');\n"
        "        if (iteration >= targetText.length) {\n"
        "           clearInterval(window.decryptInterval);\n"
        "           uiLocked = false;\n"
        "        }\n"
        "        iteration += 0.5;\n"
        "      }, 40);\n"
        "    }\n"
        "\n"
        "    function executePact() {\n"
        "      if(isShuttingDown || uiLocked) return;\n"
        "      uiLocked = true;\n"
        "      if(window.decryptInterval) clearInterval(window.decryptInterval);\n"
        "      \n"
        "      const st = document.getElementById('status-text');\n"
        "      st.className = ''; void st.offsetWidth;\n"
        "      st.innerText = ''; \n"
        "      \n"
        "      window.webkit.messageHandlers.sygilBridge.postMessage({alias: document.getElementById('alias').value, token: document.getElementById('token').value});\n"
        "    }\n"
        "\n"
        "    let formSigil = false;\n"
        "    let sigilFullyFormed = false;\n"
        "    \n"
        "    function triggerResponse(success, message) {\n"
        "      if(isShuttingDown) return;\n"
        "      const st = document.getElementById('status-text');\n"
        "      st.className = ''; void st.offsetWidth;\n"
        "      \n"
        "      if (success) {\n"
        "        uiLocked = true;\n"
        "        st.innerText = '[+] pact verified. target required.';\n"
        "        st.style.color = '#ffffff';\n"
        "        st.style.textShadow = '0 0 10px rgba(255,255,255,0.5)';\n"
        "        \n"
        "        const body = document.querySelector('.tf-body');\n"
        "        const btn = document.querySelector('button');\n"
        "        const card = document.getElementById('main-card');\n"
        "        \n"
        "        const startHeight = card.getBoundingClientRect().height;\n"
        "        card.style.height = startHeight + 'px';\n"
        "        card.style.transition = 'height 0.5s cubic-bezier(0.4, 0, 0.2, 1)';\n"
        "        card.addEventListener('transitionend', function resetCardHeight(e) {\n"
        "          if (e.propertyName === 'height') {\n"
        "            card.style.height = '';\n"
        "            card.removeEventListener('transitionend', resetCardHeight);\n"
        "          }\n"
        "        });\n"
        "        \n"
        "        body.style.animation = 'text-dissolve 0.5s ease-in forwards';\n"
        "        btn.style.animation = 'text-dissolve 0.5s ease-in forwards 0.1s';\n"
        "        \n"
        "        setTimeout(() => {\n"
        "          body.innerHTML = \n"
        "            \"<div class='input-wrapper'>\" +\n"
        "              \"<input type='text' id='hex-first' placeholder=' ' spellcheck='false' autocomplete='off'>\" +\n"
        "              \"<label>first name</label>\" +\n"
        "              \"<div class='physics-caret-container'><div class='physics-caret-inner'></div></div>\" +\n"
        "            \"</div>\" +\n"
        "            \"<div class='input-wrapper' style='margin-top: 25px;'>\" +\n"
        "              \"<input type='text' id='hex-last' placeholder=' ' spellcheck='false' autocomplete='off'>\" +\n"
        "              \"<label>last name</label>\" +\n"
        "              \"<div class='physics-caret-container'><div class='physics-caret-inner'></div></div>\" +\n"
        "            \"</div>\" +\n"
        "            \"<div class='breathing-text'>\" +\n"
        "              \"imagine their face in your head as you type out their name...\" +\n"
        "            \"</div>\";\n"
        "          btn.innerText = 'cast hex';\n"
        "          btn.onclick = executeHexTarget;\n"
        "          \n"
        "          bindCaretEvents();\n"
        "          \n"
        "          body.style.animation = 'text-fade-in 0.8s ease-out forwards';\n"
        "          btn.style.animation = 'text-fade-in 0.8s ease-out forwards';\n"
        "          \n"
        "          requestAnimationFrame(() => { card.style.height = card.scrollHeight + 'px'; });\n"
        "          \n"
        "          uiLocked = false;\n"
        "        }, 600);\n"
        "      } else {\n"
        "        st.innerText = message;\n"
        "        st.className = message.includes('name') ? 'error-short' : 'error-reject';\n"
        "        setTimeout(() => { uiLocked = false; }, 800);\n"
        "      }\n"
        "    }\n"
        "\n"
        "    function executeHexTarget() {\n"
        "      if(isShuttingDown || uiLocked) return;\n"
        "      const firstInput = document.getElementById('hex-first');\n"
        "      const lastInput = document.getElementById('hex-last');\n"
        "      \n"
        "      if(!firstInput || firstInput.value.trim().length < 1 || !lastInput || lastInput.value.trim().length < 1) {\n"
        "          const st = document.getElementById('status-text');\n"
        "          st.innerText = '[-] target required.';\n"
        "          st.className = 'error-short';\n"
        "          return;\n"
        "      }\n"
        "      \n"
        "      uiLocked = true;\n"
        "      isShuttingDown = true;\n"
        "      \n"
        "      const cvs = document.getElementById('matrix-canvas');\n"
        "      const card = document.getElementById('main-card');\n"
        "      const prom = document.getElementById('prominent-success');\n"
        "\n"
        "      document.querySelector('.tf-header').style.animation = 'text-dissolve 0.8s ease-in forwards';\n"
        "      document.querySelector('.tf-body').style.animation = 'text-dissolve 0.8s ease-in forwards 0.2s';\n"
        "      document.querySelector('button').style.animation = 'text-dissolve 0.8s ease-in forwards 0.4s';\n"
        "      document.querySelector('.tf-readout').style.animation = 'text-dissolve 0.8s ease-in forwards 0.6s';\n"
        "      \n"
        "      card.style.transition = 'background-color 2s ease';\n"
        "      card.style.backgroundColor = 'rgba(5, 5, 5, 0)';\n"
        "      \n"
        "      setTimeout(() => {\n"
        "        cvs.style.zIndex = '50';\n"
        "        window.formSigilStartTime = Date.now() * 0.001;\n"
        "        formSigil = true;\n"
        "        \n"
        "        prom.innerText = 'CURSE SEALED';\n"
        "        prom.style.opacity = '1';\n"
        "      }, 1200);\n"
        "    }\n"
        "\n"
        "    const canvas = document.getElementById('matrix-canvas');\n"
        "    const ctx = canvas.getContext('2d');\n"
        "    function resizeCanvas() { canvas.width = window.innerWidth; canvas.height = window.innerHeight; }\n"
        "    window.addEventListener('resize', resizeCanvas); resizeCanvas();\n"
        "    \n"
        "    // pentagram points, drawn as a 5-pointed star (skip-2 order) then a circle around it\n"
        "    const radius = 145 * 1.35;\n"
        "    const starLines = [];\n"
        "    const rawLines = [];\n"
        "    \n"
        "    const pts = [];\n"
        "    for(let i=0; i<5; i++) {\n"
        "        let angle = -Math.PI/2 + i * (Math.PI * 2 / 5);\n"
        "        pts.push({x: radius * Math.cos(angle), y: radius * Math.sin(angle)});\n"
        "    }\n"
        "    const order = [0, 2, 4, 1, 3];\n"
        "    for(let i=0; i<5; i++) {\n"
        "        let p1 = pts[order[i]];\n"
        "        let p2 = pts[order[(i+1)%5]];\n"
        "        let lineObj = {x1: p1.x, y1: p1.y, x2: p2.x, y2: p2.y, len: Math.hypot(p2.x-p1.x, p2.y-p1.y), isCircle: false};\n"
        "        starLines.push(lineObj);\n"
        "        rawLines.push(lineObj);\n"
        "    }\n"
        "\n"
        "    const circleSegs = 60;\n"
        "    for(let i=0; i<circleSegs; i++) {\n"
        "        let a1 = i * (Math.PI * 2 / circleSegs);\n"
        "        let a2 = (i+1) * (Math.PI * 2 / circleSegs);\n"
        "        rawLines.push({\n"
        "            x1: radius * Math.cos(a1), y1: radius * Math.sin(a1), \n"
        "            x2: radius * Math.cos(a2), y2: radius * Math.sin(a2), \n"
        "            len: Math.hypot(radius * Math.cos(a2) - radius * Math.cos(a1), radius * Math.sin(a2) - radius * Math.sin(a1)),\n"
        "            isCircle: true\n"
        "        });\n"
        "    }\n"
        "\n"
        "    let totalLength = 0;\n"
        "    rawLines.forEach(l => totalLength += l.len);\n"
        "\n"
        "    let starLength = 0;\n"
        "    starLines.forEach(l => starLength += l.len);\n"
        "\n"
        "    const chars = 'Δ ∇ ⎊ ☿ ♄ ♅ ♆ ♇ ♈ ♉ ♊ ♋ ♌ ♍ ♎ ♏ ♐ ♑ ♒ ♓ ⛧ ⸸ ⍟ ☽ ☾'.split(' ');\n"
        "    const numParticles = 420;\n"
        "    const particles = [];\n"
        "    \n"
        "    for(let i=0; i<numParticles; i++) {\n"
        "        let targetDistance = (i / numParticles) * totalLength;\n"
        "        let currentDist = 0;\n"
        "        let assignedLine = 0;\n"
        "        let localT = 0;\n"
        "        \n"
        "        for(let j=0; j<rawLines.length; j++) {\n"
        "            if (targetDistance <= currentDist + rawLines[j].len) {\n"
        "                assignedLine = j;\n"
        "                localT = (targetDistance - currentDist) / rawLines[j].len;\n"
        "                break;\n"
        "            }\n"
        "            currentDist += rawLines[j].len;\n"
        "        }\n"
        "        \n"
        "        let isStarPart = !rawLines[assignedLine].isCircle;\n"
        "        let starRelativeT = 0;\n"
        "        if (isStarPart) {\n"
        "            let distSoFar = 0;\n"
        "            for(let k=0; k<assignedLine; k++) distSoFar += starLines[k].len;\n"
        "            distSoFar += starLines[assignedLine].len * localT;\n"
        "            starRelativeT = distSoFar / starLength;\n"
        "        }\n"
        "        \n"
        "        let pathFraction = targetDistance / totalLength;\n"
        "        \n"
        "        particles.push({\n"
        "            x: Math.random() * canvas.width,\n"
        "            y: Math.random() * canvas.height,\n"
        "            speed: 0.1 + Math.random() * 0.3,\n"
        "            char: chars[Math.floor(Math.random() * chars.length)] + '\\uFE0E',\n"
        "            phase: Math.random() * Math.PI * 2,\n"
        "            lineIndex: assignedLine,\n"
        "            fixedT: localT,\n"
        "            isCircle: rawLines[assignedLine].isCircle,\n"
        "            isStar: isStarPart,\n"
        "            starT: starRelativeT,\n"
        "            easeSpeed: 0.003 + (Math.random() * 0.003),\n"
        "            progress: 0,\n"
        "            lockedStart: false,\n"
        "            startX: 0,\n"
        "            startY: 0,\n"
        "            pathFraction: pathFraction,\n"
        "            arrivalDelay: pathFraction * 1.1 + Math.random() * 0.2,\n"
        "            landTime: null\n"
        "        });\n"
        "    }\n"
        "    \n"
        "    let animationStartTime = 0;\n"
        "    \n"
        "    const NORMAL_FONT = '15px \"JetBrains Mono\", monospace';\n"
        "    \n"
        "    function drawMatrix() {\n"
        "      requestAnimationFrame(drawMatrix);\n"
        "      let rawTime = Date.now() * 0.001;\n"
        "      \n"
        "      if (typeof window.accumulatedAngle === 'undefined') {\n"
        "          window.accumulatedAngle = 0;\n"
        "          window.lastFrameTime = rawTime;\n"
        "      }\n"
        "      let dt = rawTime - window.lastFrameTime;\n"
        "      window.lastFrameTime = rawTime;\n"
        "      if (dt > 0.1) dt = 0.016; // clamp so tab-switching doesn't nuke the animation\n"
        "      \n"
        "      window.accumulatedAngle += dt * 0.25;\n"
        "      let globalAngle = window.accumulatedAngle;\n"
        "      let cosA = Math.cos(globalAngle);\n"
        "      let sinA = Math.sin(globalAngle);\n"
        "      \n"
        "      ctx.clearRect(0, 0, canvas.width, canvas.height);\n"
        "      ctx.font = NORMAL_FONT;\n"
        "      ctx.globalCompositeOperation = 'screen'; \n"
        "      \n"
        "      let cx = canvas.width / 2;\n"
        "      let cy = canvas.height / 2;\n"
        "      \n"
        "      let allParticlesInPlace = true;\n"
        "      \n"
        "      for(let i = 0; i < particles.length; i++) {\n"
        "        let p = particles[i];\n"
        "        \n"
        "        if (formSigil) {\n"
        "          if (!p.lockedStart) {\n"
        "              p.startX = p.x;\n"
        "              p.startY = p.y;\n"
        "              p.lockedStart = true;\n"
        "          }\n"
        "          \n"
        "          let line = rawLines[p.lineIndex];\n"
        "          \n"
        "          let localX = (line.x1 + (line.x2 - line.x1) * p.fixedT);\n"
        "          let localY = (line.y1 + (line.y2 - line.y1) * p.fixedT);\n"
        "          \n"
        "          let baseTargetX = cx + (localX * cosA - localY * sinA);\n"
        "          let baseTargetY = cy + (localX * sinA + localY * cosA);\n"
        "          \n"
        "          let sinceForm = rawTime - (window.formSigilStartTime || rawTime);\n"
        "          \n"
        "          if (p.progress < 1.0 && sinceForm >= p.arrivalDelay) {\n"
        "              p.progress += p.easeSpeed;\n"
        "              if (p.progress > 1.0) p.progress = 1.0;\n"
        "          }\n"
        "          if (p.progress < 1.0) allParticlesInPlace = false;\n"
        "          \n"
        "          let easeOut = 1 - Math.pow(1 - p.progress, 3);\n"
        "          \n"
        "          p.x = p.startX + (baseTargetX - p.startX) * easeOut;\n"
        "          p.y = p.startY + (baseTargetY - p.startY) * easeOut;\n"
        "          \n"
        "          if (!sigilFullyFormed) {\n"
        "             let dGlow;\n"
        "             if (p.progress < 1.0) {\n"
        "                 dGlow = p.progress * p.progress;\n"
        "             } else {\n"
        "                 if (p.landTime === null) p.landTime = rawTime;\n"
        "                 dGlow = Math.max(0, 1 - (rawTime - p.landTime) / 0.6);\n"
        "             }\n"
        "             let driftAlpha = 0.3 + dGlow * 0.55;\n"
        "             ctx.fillStyle = `rgba(255, 255, 255, ${driftAlpha})`;\n"
        "             ctx.fillText(p.char, p.x, p.y);\n"
        "          } else {\n"
        "             let pulseTime = rawTime - animationStartTime;\n"
        "             let settleBlend = Math.min(1, pulseTime / 0.6);\n"
        "             let dGlowOld = p.landTime !== null ? Math.max(0, 1 - (rawTime - p.landTime) / 0.6) : 0;\n"
        "             let driftAlphaOld = 0.3 + dGlowOld * 0.55;\n"
        "             \n"
        "             let ambient = 0.2 + (Math.sin(p.phase + rawTime * 1.5) + 1) * 0.15;\n"
        "             let alpha = driftAlphaOld * (1 - settleBlend) + ambient * settleBlend;\n"
        "             ctx.fillStyle = `rgba(255, 255, 255, ${alpha})`;\n"
        "             ctx.fillText(p.char, p.x, p.y);\n"
        "          }\n"
        "        } else {\n"
        "          p.y += p.speed;\n"
        "          p.phase += 0.02;\n"
        "          if(p.y > canvas.height + 20) { \n"
        "             p.y = -20; \n"
        "             p.x = Math.random() * canvas.width; \n"
        "          }\n"
        "          let alpha = 0.15 + (Math.sin(p.phase * 1.5) + 1) * 0.2;\n"
        "          ctx.fillStyle = `rgba(255, 255, 255, ${alpha})`;\n"
        "          ctx.fillText(p.char, p.x, p.y);\n"
        "        }\n"
        "      }\n"
        "      \n"
        "      if (formSigil && !sigilFullyFormed && allParticlesInPlace) {\n"
        "          sigilFullyFormed = true;\n"
        "          animationStartTime = rawTime;\n"
        "      }\n"
        "    }\n"
        "    requestAnimationFrame(drawMatrix);\n"
        "  </script>\n"
        "</body>\n"
        "</html>\n";

    webkit_web_view_load_html(WEBKIT_WEB_VIEW(web_view), html_content, NULL);
    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("org.sygil.node", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", (GCallback)activate, NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}