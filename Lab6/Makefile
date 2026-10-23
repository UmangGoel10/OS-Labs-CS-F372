.PHONY: all test test-private clean

all:
	@python3 grader.py build

test:
	@python3 grader.py test --suite public --n "$(N)"

test-private:
	@python3 grader.py test --suite private --n "$(N)"

clean:
	@rm -rf .build
	@echo "Removed build files."
