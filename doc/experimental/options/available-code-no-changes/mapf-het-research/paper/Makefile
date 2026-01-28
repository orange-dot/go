# Makefile for MAPF-HET Paper

MAIN = main
LATEX = pdflatex
BIBTEX = bibtex

.PHONY: all clean view

all: $(MAIN).pdf

$(MAIN).pdf: $(MAIN).tex sections/*.tex references.bib
	$(LATEX) $(MAIN)
	$(BIBTEX) $(MAIN)
	$(LATEX) $(MAIN)
	$(LATEX) $(MAIN)

quick:
	$(LATEX) $(MAIN)

clean:
	rm -f $(MAIN).aux $(MAIN).bbl $(MAIN).blg $(MAIN).log $(MAIN).out $(MAIN).pdf
	rm -f sections/*.aux

view: $(MAIN).pdf
	@if command -v xdg-open > /dev/null; then xdg-open $(MAIN).pdf; \
	elif command -v open > /dev/null; then open $(MAIN).pdf; \
	else echo "Please open $(MAIN).pdf manually"; fi

# Word count approximation
wc:
	@texcount -inc -total $(MAIN).tex 2>/dev/null || echo "Install texcount for word count"
