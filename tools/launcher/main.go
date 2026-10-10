// TempCtl Duo launcher (spec 7b): one portable executable that serves the Web Serial page on
// http://localhost:8765 (a fixed port, so the browser remembers the granted serial port) and opens it
// in Chrome or Edge as an app window. Nothing is installed. It also searches the network for devices
// (discover.go) for the connect screen's Wi-Fi list. The page tells the launcher that it is
// still open (/launcher/alive every 5 s); 30 s after the last window closed the launcher ends.
//
// Build: tools/launcher/build.sh (copies the page from web/ with tools/build_site.py, then go build).
package main

import (
	"bytes"
	"embed"
	"fmt"
	"io/fs"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync/atomic"
	"time"
)

//go:embed site
var siteFS embed.FS

var version = "dev" // set by build.sh from src/version.h

const (
	port    = 8765
	origin  = "http://localhost:8765/"
	idleEnd = 30 * time.Second
)

var lastAlive atomic.Int64 // unix ms of the last /launcher/alive (0: no window yet)

func main() {
	site, _ := fs.Sub(siteFS, "site")
	page, err := fs.ReadFile(site, "index.html")
	shell, err2 := fs.ReadFile(site, "shell.html")
	if err != nil || err2 != nil {
		fail("The page is missing in this build.")
	}
	// the launcher's copies: they know to call /launcher/alive (and the page: to list found devices)
	mark := func(b []byte) []byte {
		return bytes.Replace(b, []byte(`<html lang="en">`), []byte(`<html lang="en" data-launcher>`), 1)
	}
	page, shell = mark(page), mark(shell)

	ln4, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", port))
	if err != nil {
		if isOurs() { // a launcher runs already: one more window
			openWindow()
			return
		}
		fail(fmt.Sprintf("Port %d on this computer is used by another program.\n"+
			"TempCtl Duo needs this port (the browser remembers the USB permission for it).\n"+
			"Close the other program and start TempCtl Duo again.", port))
	}
	ln6, _ := net.Listen("tcp", fmt.Sprintf("[::1]:%d", port)) // (localhost may resolve to ::1)

	mux := http.NewServeMux()
	files := http.FileServer(http.FS(site))
	mux.HandleFunc("/launcher/id", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		fmt.Fprintf(w, "tempctl-duo-launcher %s", version)
	})
	mux.HandleFunc("/launcher/devices", handleDevices)
	mux.HandleFunc("/launcher/alive", func(w http.ResponseWriter, r *http.Request) {
		lastAlive.Store(time.Now().UnixMilli())
		w.Header().Set("Cache-Control", "no-store")
		w.WriteHeader(http.StatusNoContent)
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-cache")
		switch r.URL.Path {
		case "/", "/shell.html": // the window: tabs, one device each
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			w.Write(shell)
			return
		case "/app", "/index.html": // the page in a tab
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			w.Write(page)
			return
		}
		if strings.HasPrefix(r.URL.Path, "/api/") { // no device here: the page uses USB
			http.NotFound(w, r)
			return
		}
		files.ServeHTTP(w, r)
	})
	srv := &http.Server{Handler: mux, ReadHeaderTimeout: 10 * time.Second}
	go srv.Serve(ln4)
	if ln6 != nil {
		go srv.Serve(ln6)
	}
	openWindow()

	// end when no window has reported for a while (or none ever opened within 2 minutes)
	start := time.Now()
	for range time.Tick(2 * time.Second) {
		a := lastAlive.Load()
		if a == 0 && time.Since(start) > 2*time.Minute {
			return
		}
		if a != 0 && time.Since(time.UnixMilli(a)) > idleEnd {
			return
		}
	}
}

// a launcher answers on the port already
func isOurs() bool {
	c := http.Client{Timeout: 2 * time.Second}
	r, err := c.Get(fmt.Sprintf("http://127.0.0.1:%d/launcher/id", port))
	if err != nil {
		return false
	}
	defer r.Body.Close()
	b := make([]byte, 64)
	n, _ := r.Body.Read(b)
	return strings.HasPrefix(string(b[:n]), "tempctl-duo-launcher")
}

// Chrome or Edge as an app window (Web Serial needs one of them); else the default browser
func openWindow() {
	args := []string{"--app=" + origin, "--new-window"}
	for _, b := range browsers() {
		if _, err := os.Stat(b); err == nil || !strings.ContainsRune(b, os.PathSeparator) {
			if exec.Command(b, args...).Start() == nil {
				return
			}
		}
	}
	switch runtime.GOOS {
	case "windows":
		exec.Command("rundll32", "url.dll,FileProtocolHandler", origin).Start()
	case "darwin":
		exec.Command("open", origin).Start()
	default:
		exec.Command("xdg-open", origin).Start()
	}
}

func browsers() []string {
	switch runtime.GOOS {
	case "windows":
		var out []string
		for _, base := range []string{os.Getenv("ProgramFiles"), os.Getenv("ProgramFiles(x86)"), os.Getenv("LocalAppData")} {
			if base == "" {
				continue
			}
			out = append(out, filepath.Join(base, `Google\Chrome\Application\chrome.exe`))
		}
		for _, base := range []string{os.Getenv("ProgramFiles(x86)"), os.Getenv("ProgramFiles")} {
			if base != "" {
				out = append(out, filepath.Join(base, `Microsoft\Edge\Application\msedge.exe`))
			}
		}
		return out
	case "darwin":
		return []string{"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
			"/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"}
	default:
		return []string{"google-chrome", "chromium", "chromium-browser", "microsoft-edge"}
	}
}

func fail(msg string) {
	showError("TempCtl Duo "+version, msg)
	os.Exit(1)
}
