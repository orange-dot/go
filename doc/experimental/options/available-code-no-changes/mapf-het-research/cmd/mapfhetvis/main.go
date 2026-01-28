// Command mapfhetvis provides a GUI visualization for MAPF-HET algorithms.
package main

import (
	"log"
	"os"

	"gioui.org/app"
	"gioui.org/unit"

	"github.com/elektrokombinacija/mapf-het-research/internal/vis"
)

func main() {
	go func() {
		window := new(app.Window)
		window.Option(
			app.Title("MAPF-HET Visualizer"),
			app.Size(unit.Dp(1400), unit.Dp(900)),
		)

		application := vis.NewApp()
		if err := application.Run(window); err != nil {
			log.Fatal(err)
		}
		os.Exit(0)
	}()
	app.Main()
}
