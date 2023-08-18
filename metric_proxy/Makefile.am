SUBDIRS=include src tests


archive:
	git -C $(top_srcdir) archive --format=tar --prefix=$(PACKAGE)-$(VERSION)/ HEAD | gzip > $(PACKAGE)-$(VERSION).tar.gz
