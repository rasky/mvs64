package main

import (
	"bufio"
	"compress/gzip"
	"encoding/binary"
	"encoding/json"
	"os"
	"path/filepath"
)

type M68kState struct {
	D0, D1, D2, D3, D4, D5, D6, D7 uint32
	A0, A1, A2, A3, A4, A5, A6     uint32
	USP, SSP                       uint32
	SR                             uint32
	PC                             uint32
	Prefetch                       [2]uint32
	Ram                            [][2]uint32
}

type Test struct {
	Name    string
	Initial M68kState
	Final   M68kState
	Length  int
}

func readTests(fn string) []Test {
	f, err := os.Open(fn)
	if err != nil {
		panic(err)
	}
	defer f.Close()
	gz, err := gzip.NewReader(f)
	if err != nil {
		panic(err)
	}
	defer gz.Close()

	var tests []Test
	err = json.NewDecoder(gz).Decode(&tests)
	if err != nil {
		panic(err)
	}

	return tests
}

func writeTests(fn string, tests []Test) {
	f, err := os.Create(fn)
	if err != nil {
		panic(err)
	}
	defer f.Close()

	ff := bufio.NewWriter(f)
	defer ff.Flush()

	w32 := func(v uint32) {
		var x [4]byte
		binary.BigEndian.PutUint32(x[:], v)
		ff.Write(x[:])
	}

	writeState := func(s *M68kState) {
		w32(s.D0)
		w32(s.D1)
		w32(s.D2)
		w32(s.D3)
		w32(s.D4)
		w32(s.D5)
		w32(s.D6)
		w32(s.D7)
		w32(s.A0)
		w32(s.A1)
		w32(s.A2)
		w32(s.A3)
		w32(s.A4)
		w32(s.A5)
		w32(s.A6)
		w32(s.USP)
		w32(s.SSP)
		w32(s.SR)
		w32(s.PC)
		w32(s.Prefetch[0])
		w32(s.Prefetch[1])
		w32(uint32(len(s.Ram)))
		for _, r := range s.Ram {
			w32(r[0])
			w32(r[1])
		}
	}

	ff.Write([]byte{0x4d, 0x36, 0x34, 0x4b})
	w32(uint32(len(tests)))
	for _, test := range tests {
		ff.Write([]byte{0x54, 0x45, 0x53, 0x54})
		w32(uint32(len(test.Name)))
		ff.Write([]byte(test.Name))
		w32(uint32(test.Length))
		writeState(&test.Initial)
		writeState(&test.Final)
	}
}

func main() {
	files, err := filepath.Glob("*.json.gz")
	if err != nil {
		panic(err)
	}
	for _, fn := range files {
		tests := readTests(fn)
		fn = fn[:len(fn)-len(".json.gz")] + ".btest"
		writeTests(fn, tests)
	}
}
