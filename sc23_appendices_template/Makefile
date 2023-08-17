BASENAME = Appendix_template

all: pdf


pdf: $(BASENAME).tex $(FIGURES) $(STYLEFILES)
	pdflatex -shell-escape $(BASENAME).tex
	bibtex $(BASENAME) 2&1> /dev/null
	pdflatex  -shell-escape $(BASENAME).tex
	pdflatex  -shell-escape $(BASENAME).tex
	@echo "\033[91m\n\n---------------------------------------\033[0m"
	@echo "\033[92mtarget \033[91m$(BASENAME).$@ \033[92mfinished successfully\033[0m"
	@echo "\033[91m---------------------------------------\n\033[0m"

zip:
	make clean
	tgz $(BASENAME).tgz Makefile *.*


clean:
	@rm -f  *~  
	@rm -f  *.aux *.bbl *.blg *.dvi *.log  *.pdfsync  *.ps *.tpt *.tgz
	@rm -f $(BASENAME).pdf 
	@rm -f  *~
	@echo "\n\033[92mtarget clean finished\n\033[0m" 
show:
	evince $(BASENAME).pdf

force: 
	touch $(BASENAME).tex
	make pdf

AD: appendix

appendix: AD.pdf

AD.pdf: AD.tex
	pdflatex AD.tex
