# The Cmajor Compiler Toolkit - Tests

This folder contains some of the tests that we use to check the correctness and performance of the compiler.

The many `.cmajtest` files should be helpful to anyone looking for examples of how these files are structured.

You can run all the tests with the command:

```
$ cmaj test /path_to_my_cmajor_repo/tests
```

...and hopefully they should all pass!

To see the other command-line options for running tests (e.g. number of threads, back-end, etc), run `cmaj --help`

- `language_tests` - this folder contains tests that sanity-check the parser and compiler's handling of language constructs
- `integration_tests` - this folder contains tests that run sample data through some processors and check that the output is what was expected
- `performance_tests` - this folder contains tests that measure performance of some Cmajor algorithms. Obviously the results will vary wildy depending on the platform, backend, compiler build, etc. (When running performance tests, it's probably wise to always use `--singleThread` to get more consistent results)
