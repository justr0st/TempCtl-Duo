package main

// Device search for the connect screen: mDNS queries for _tempctl._tcp (TempCtl Duo 1.0.27+) and
// _http._tcp (older firmware) on every IPv4 network of this computer, each answer checked by reading
// the device's /api/live. Sockets bound to the network addresses: Windows asks once whether the
// launcher may use the network ("Allow access" on private networks).
//
// The queries come from an ordinary port, so the devices answer straight to it (RFC 6762 6.7,
// "legacy unicast"); no multicast listening is needed.

import (
	"encoding/binary"
	"encoding/json"
	"net"
	"net/http"
	"sort"
	"strings"
	"sync"
	"time"
)

type Device struct {
	Host    string `json:"host"`
	IP      string `json:"ip"`
	Port    int    `json:"port"`
	FW      string `json:"fw"`
	Board   string `json:"board"`
	Variant string `json:"variant"`
	Net     string `json:"net"`
	seen    time.Time
}

var (
	devMu     sync.Mutex
	devices   = map[string]*Device{} // by ip:port
	scanMu    sync.Mutex
	lastScan  time.Time
	scanning  bool
	checkedAt = map[string]time.Time{} // ip:port -> last /api/live check
)

var services = []string{"_tempctl._tcp.local.", "_http._tcp.local."}

// /launcher/devices: the devices seen in the last 30 s; starts a search (at most every 4 s)
func handleDevices(w http.ResponseWriter, r *http.Request) {
	startScan()
	devMu.Lock()
	out := []Device{}
	for k, d := range devices {
		if time.Since(d.seen) > 30*time.Second {
			delete(devices, k)
			continue
		}
		out = append(out, *d)
	}
	devMu.Unlock()
	sort.Slice(out, func(i, j int) bool { return out[i].Host < out[j].Host })
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	scanMu.Lock()
	busy := scanning
	scanMu.Unlock()
	json.NewEncoder(w).Encode(map[string]any{"devices": out, "searching": busy})
}

func startScan() {
	scanMu.Lock()
	defer scanMu.Unlock()
	if scanning || time.Since(lastScan) < 4*time.Second {
		return
	}
	scanning = true
	lastScan = time.Now()
	go func() {
		scan()
		scanMu.Lock()
		scanning = false
		scanMu.Unlock()
	}()
}

func localIPv4s() []net.IP {
	var out []net.IP
	ifs, _ := net.Interfaces()
	for _, i := range ifs {
		if i.Flags&net.FlagUp == 0 || i.Flags&net.FlagLoopback != 0 || i.Flags&net.FlagMulticast == 0 {
			continue
		}
		addrs, _ := i.Addrs()
		for _, a := range addrs {
			if n, ok := a.(*net.IPNet); ok && n.IP.To4() != nil && !n.IP.IsLinkLocalUnicast() {
				out = append(out, n.IP.To4())
			}
		}
	}
	return out
}

func scan() {
	var wg sync.WaitGroup
	for _, ip := range localIPv4s() {
		wg.Add(1)
		go func(ip net.IP) {
			defer wg.Done()
			scanFrom(ip)
		}(ip)
	}
	wg.Wait()
}

// one network: two queries, answers for 2.5 s
func scanFrom(local net.IP) {
	c, err := net.ListenUDP("udp4", &net.UDPAddr{IP: local})
	if err != nil {
		return
	}
	defer c.Close()
	q := query(services)
	group := &net.UDPAddr{IP: net.IPv4(224, 0, 0, 251), Port: 5353}
	c.WriteToUDP(q, group)
	time.Sleep(300 * time.Millisecond)
	c.WriteToUDP(q, group)
	c.SetReadDeadline(time.Now().Add(2500 * time.Millisecond))
	buf := make([]byte, 9000)
	for {
		n, from, err := c.ReadFromUDP(buf)
		if err != nil {
			return
		}
		host, port, ok := parseAnswer(buf[:n])
		if ok {
			go check(from.IP.String(), port, host)
		}
	}
}

// the answer must be from a TempCtl Duo: its /api/live tells (checked at most every 20 s)
func check(ip string, port int, host string) {
	if port == 0 {
		port = 80
	}
	key := ip + ":" + itoa(port)
	devMu.Lock()
	if d, ok := devices[key]; ok {
		d.seen = time.Now()
		if host != "" {
			d.Host = host
		}
	}
	if t, ok := checkedAt[key]; ok && time.Since(t) < 20*time.Second {
		devMu.Unlock()
		return
	}
	checkedAt[key] = time.Now()
	devMu.Unlock()
	cl := http.Client{Timeout: 3 * time.Second}
	r, err := cl.Get("http://" + key + "/api/live")
	if err != nil {
		return
	}
	defer r.Body.Close()
	var live struct {
		FW      string `json:"fw"`
		Board   string `json:"board"`
		Variant string `json:"variant"`
		Net     string `json:"net"`
		Run     *struct {
			State string `json:"state"`
		} `json:"run"`
	}
	if json.NewDecoder(r.Body).Decode(&live) != nil || live.FW == "" || live.Run == nil {
		return // some other web server
	}
	if host == "" {
		host = ip
	}
	devMu.Lock()
	devices[key] = &Device{Host: host, IP: ip, Port: port, FW: live.FW, Board: live.Board, Variant: live.Variant, Net: live.Net, seen: time.Now()}
	devMu.Unlock()
}

func itoa(i int) string {
	b := [8]byte{}
	n := len(b)
	for {
		n--
		b[n] = byte('0' + i%10)
		i /= 10
		if i == 0 {
			break
		}
	}
	return string(b[n:])
}

// ---- DNS messages (only what the search needs)

func putName(b []byte, name string) []byte {
	for _, l := range strings.Split(strings.TrimSuffix(name, "."), ".") {
		b = append(b, byte(len(l)))
		b = append(b, l...)
	}
	return append(b, 0)
}

func query(names []string) []byte {
	b := make([]byte, 12)
	binary.BigEndian.PutUint16(b[4:], uint16(len(names)))
	for _, n := range names {
		b = putName(b, n)
		b = append(b, 0, 12, 0, 1) // PTR, IN
	}
	return b
}

// name at off (with compression); returns the name and the offset after it
func readName(m []byte, off int) (string, int, bool) {
	var parts []string
	end, jumps := -1, 0
	for {
		if off >= len(m) {
			return "", 0, false
		}
		l := int(m[off])
		switch {
		case l == 0:
			if end < 0 {
				end = off + 1
			}
			return strings.Join(parts, ".") + ".", end, true
		case l&0xC0 == 0xC0:
			if off+1 >= len(m) || jumps > 20 {
				return "", 0, false
			}
			if end < 0 {
				end = off + 2
			}
			off = int(binary.BigEndian.Uint16(m[off:]) & 0x3FFF)
			jumps++
		default:
			if off+1+l > len(m) {
				return "", 0, false
			}
			parts = append(parts, string(m[off+1:off+1+l]))
			off += 1 + l
		}
	}
}

// An answer that names one of our services (PTR); host (from SRV / A, without ".local") and port if given
func parseAnswer(m []byte) (host string, port int, ok bool) {
	if len(m) < 12 || m[2]&0x80 == 0 { // not a response
		return "", 0, false
	}
	qd := int(binary.BigEndian.Uint16(m[4:]))
	rr := int(binary.BigEndian.Uint16(m[6:])) + int(binary.BigEndian.Uint16(m[8:])) + int(binary.BigEndian.Uint16(m[10:]))
	off := 12
	for i := 0; i < qd; i++ {
		_, o, good := readName(m, off)
		if !good {
			return "", 0, false
		}
		off = o + 4
	}
	for i := 0; i < rr; i++ {
		name, o, good := readName(m, off)
		if !good || o+10 > len(m) {
			return host, port, ok
		}
		typ := binary.BigEndian.Uint16(m[o:])
		rdl := int(binary.BigEndian.Uint16(m[o+8:]))
		rd := o + 10
		if rd+rdl > len(m) {
			return host, port, ok
		}
		lname := strings.ToLower(name)
		switch typ {
		case 12: // PTR
			for _, s := range services {
				if lname == s {
					ok = true
				}
			}
		case 33: // SRV: priority, weight, port, target
			if rdl >= 7 {
				port = int(binary.BigEndian.Uint16(m[rd+4:]))
				if t, _, good := readName(m, rd+6); good {
					host = strings.TrimSuffix(strings.TrimSuffix(t, "."), ".local")
				}
			}
		case 1: // A
			if host == "" {
				host = strings.TrimSuffix(strings.TrimSuffix(name, "."), ".local")
			}
		}
		off = rd + rdl
	}
	return host, port, ok
}
