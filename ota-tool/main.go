package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"html/template"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

type EnvInfo struct {
	Name  string `json:"name"`
	Board string `json:"board"`
	OTA   bool   `json:"ota"`
}

type BuildRequest struct {
	Env    string `json:"env"`
	IP     string `json:"ip"`
	Action string `json:"action"`
}

var (
	projectDir string
	envs       []EnvInfo
	reBoard    = regexp.MustCompile(`board\s*=\s*(.+)`)
	reEnv      = regexp.MustCompile(`\[env:(.+)\]`)
)

type outputStore struct {
	mu   sync.Mutex
	buf  map[string]string
	ch   map[string]chan string
}

var store = &outputStore{
	buf: make(map[string]string),
	ch:  make(map[string]chan string),
}

func (s *outputStore) append(sessionID, data string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.buf[sessionID] += data
	if len(s.buf[sessionID]) > 50000 {
		s.buf[sessionID] = s.buf[sessionID][len(s.buf[sessionID])-40000:]
	}
	if ch, ok := s.ch[sessionID]; ok {
		select {
		case ch <- data:
		default:
		}
	}
}

func (s *outputStore) getBuf(sessionID string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.buf[sessionID]
}

func (s *outputStore) subscribe(sessionID string) chan string {
	s.mu.Lock()
	defer s.mu.Unlock()
	ch := make(chan string, 200)
	s.ch[sessionID] = ch
	return ch
}

func (s *outputStore) unsubscribe(sessionID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if ch, ok := s.ch[sessionID]; ok {
		close(ch)
		delete(s.ch, sessionID)
	}
}

func init() {
	var err error
	projectDir, err = filepath.Abs(filepath.Dir(os.Args[0]))
	if err != nil {
		projectDir = "."
	}
}

func main() {
	loadEnvironments()

	http.HandleFunc("/", handleIndex)
	http.HandleFunc("/api/envs", handleEnvs)
	http.HandleFunc("/api/run", handleRun)
	http.HandleFunc("/api/stream", handleStream)

	port := 8080
	for i := 0; i < 10; i++ {
		addr := fmt.Sprintf(":%d", port+i)
		url := fmt.Sprintf("http://localhost%s", addr)

		fmt.Printf("\n")
		fmt.Printf("  ┌─────────────────────────────────┐\n")
		fmt.Printf("  │  🚀 ESP-DeskDeck OTA Tool       │\n")
		fmt.Printf("  │  📡 %s        │\n", url)
		fmt.Printf("  │  ─────────────────────────────   │\n")
		fmt.Printf("  │  Premi Ctrl+C per uscire         │\n")
		fmt.Printf("  └─────────────────────────────────┘\n")
		fmt.Printf("\n")

		go func() {
			time.Sleep(400 * time.Millisecond)
			openBrowser(url)
		}()

		if err := http.ListenAndServe(addr, nil); err != nil {
			continue
		}
		break
	}
}

func loadEnvironments() {
	envs = nil
	dir := findProjectDir()
	iniPath := filepath.Join(dir, "platformio.ini")
	data, err := os.ReadFile(iniPath)
	if err != nil {
		envs = append(envs, EnvInfo{Name: "esp32-c3-dev-ota", Board: "esp32-c3-devkitm-1", OTA: true})
		return
	}

	lines := strings.Split(string(data), "\n")
	var current string
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if m := reEnv.FindStringSubmatch(line); m != nil {
			current = m[1]
			envs = append(envs, EnvInfo{Name: current, Board: "?", OTA: strings.Contains(strings.ToLower(current), "ota")})
		} else if m := reBoard.FindStringSubmatch(line); m != nil && current != "" {
			for i := range envs {
				if envs[i].Name == current {
					envs[i].Board = m[1]
				}
			}
		}
	}
	if len(envs) == 0 {
		envs = append(envs, EnvInfo{Name: "esp32-c3-dev-ota", Board: "esp32-c3-devkitm-1", OTA: true})
	}
}

func findProjectDir() string {
	dir := projectDir
	for i := 0; i < 4; i++ {
		if _, err := os.Stat(filepath.Join(dir, "platformio.ini")); err == nil {
			return dir
		}
		dir = filepath.Dir(dir)
	}
	return projectDir
}

func handleIndex(w http.ResponseWriter, r *http.Request) {
	tmpl, _ := template.New("index").Parse(EmbeddedHTML)
	tmpl.Execute(w, nil)
}

func handleEnvs(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(envs)
}

func handleRun(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST only", 405)
		return
	}
	var req BuildRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	sessionID := strconv.FormatInt(time.Now().UnixNano(), 36)
	go runBuild(sessionID, req)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"session": sessionID})
}

func runBuild(sessionID string, req BuildRequest) {
	dir := findProjectDir()

	store.append(sessionID, fmt.Sprintf("✦ ESP-DeskDeck OTA Tool\n"))
	store.append(sessionID, fmt.Sprintf("✦ Env: [%s] | Target: %s\n", req.Env, req.IP))
	store.append(sessionID, fmt.Sprintf("✦ %s\n\n", time.Now().Format("02/01/2006 15:04:05")))

	if req.Action == "build" || req.Action == "all" {
		store.append(sessionID, "⚙️  Compilazione in corso...\n")
		runCmd(sessionID, dir, "pio", "run", "-e", req.Env)
		store.append(sessionID, "\n✅ Compilazione completata\n\n")
	}

	if req.Action == "upload" || req.Action == "all" {
		store.append(sessionID, fmt.Sprintf("📡 Upload OTA verso %s...\n", req.IP))
		runCmd(sessionID, dir, "pio", "run", "-e", req.Env, "--target", "upload", "--upload-port", req.IP)
		store.append(sessionID, "\n✅ Upload completato!\n")
	}

	store.append(sessionID, "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n")
	store.append(sessionID, "✨ Operazione completata!\n")
	store.unsubscribe(sessionID)
}

func runCmd(sessionID, dir, name string, arg ...string) {
	cmd := exec.Command(name, arg...)
	cmd.Dir = dir

	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()
	cmd.Start()

	var wg sync.WaitGroup
	wg.Add(2)

	readPipe := func(reader io.Reader) {
		defer wg.Done()
		s := bufio.NewScanner(reader)
		s.Buffer(make([]byte, 65536), 65536)
		for s.Scan() {
			store.append(sessionID, s.Text()+"\n")
		}
	}

	go readPipe(stdout)
	go readPipe(stderr)
	wg.Wait()

	cmd.Wait()
}

func handleStream(w http.ResponseWriter, r *http.Request) {
	sessionID := r.URL.Query().Get("session")
	if sessionID == "" {
		http.Error(w, "missing session", 400)
		return
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", 500)
		return
	}

	existing := store.getBuf(sessionID)
	if existing != "" {
		for _, line := range strings.Split(existing, "\n") {
			fmt.Fprintf(w, "data: %s\n", line)
		}
		fmt.Fprintf(w, "\n")
		flusher.Flush()
	}

	ch := store.subscribe(sessionID)
	ctx := r.Context()

	for {
		select {
		case <-ctx.Done():
			return
		case msg, ok := <-ch:
			if !ok {
				fmt.Fprintf(w, "event: done\ndata: \n\n")
				flusher.Flush()
				return
			}
			fmt.Fprintf(w, "data: %s\n", strings.TrimSuffix(msg, "\n"))
			flusher.Flush()
		}
	}
}

func openBrowser(url string) {
	var cmd *exec.Cmd
	if isWindows() {
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	} else {
		cmd = exec.Command("xdg-open", url)
	}
	cmd.Run()
}

func isWindows() bool {
	return len(os.Getenv("WINDIR")) > 0 || strings.HasPrefix(os.Getenv("OS"), "Windows")
}
