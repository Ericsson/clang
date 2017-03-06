#!/usr/bin/env python

'''
   Script to merge gcov files produced by the static analyzer.
   So coverage information of header files from multiple translation units are
   merged together. The output can be pocessed by gcovr format like:
        gcovr -g outputdir --html --html-details -r sourceroot -o example.html
   The expected layout of input (the input should be the gcovdir):
        gcovdir/TranslationUnit1/file1.gcov
        gcovdir/TranslationUnit1/subdir/file2.gcov
        ...
        gcovdir/TranslationUnit1/fileN.gcov
        ...
        gcovdir/TranslationUnitK/fileM.gcov
   The output:
        outputdir/file1.gcov
        outputdir/subdir/file2.gcov
        ...
        outputdir/fileM.gcov
'''

import argparse
import os
import sys
import shutil


def is_num(val):
    '''Check if val can be converted to int.'''
    try:
        int(val)
        return True
    except ValueError:
        return False


def is_valid(line):
    '''Check whether a list is a valid gcov line after join on colon.
       Note that the source code might also containt colons, so the checks
       on the length is not strict. This could be improved in the future.'''
    if len(line) < 3:
        return False
    if line[2].lower() in {"graph", "data", "runs", "programs",
                                   "source"}:
        return len(line) == 4
    return True


def merge_gcov(from_gcov, to_gcov):
    '''Merge to existing gcov file, modify the second one.'''
    with open(from_gcov) as from_file, open(to_gcov) as to_file:
        from_lines = from_file.readlines()
        to_lines = to_file.readlines()

        if len(from_lines) != len(to_lines):
            print("Fatal error: failed to match gcov files,"
                  " different line count: (%s, %s)" %
                  (from_gcov, to_gcov))
            sys.exit(1)

        for i in range(len(from_lines)):
            from_split = from_lines[i].split(":")
            to_split = to_lines[i].split(":")

            if not is_valid(from_split) or not is_valid(to_split):
                print("Fatal error: invalid gcov format (%s, %s)" %
                      (from_gcov, to_gcov))
                print("%s, %s" % (from_split, to_split))
                sys.exit(1)

            for j in range(2):
                if from_split[j+1] != to_split[j+1]:
                    print("Fatal error: failed to match gcov files: (%s, %s)" %
                          (from_gcov, to_gcov))
                    print("%s != %s" % (from_split[j+1], to_split[j+1]))
                    sys.exit(1)

            if to_split[0] == '#####':
                if from_split[0] != '-':
                    to_split[0] = from_split[0]
            elif to_split[0] == '-':
                assert is_num(from_split[0]) or from_split[0] == '#####' \
                       or from_split[0] == '-'
                to_split[0] = from_split[0]
            elif is_num(to_split[0]):
                assert is_num(from_split[0]) or from_split[0] == '#####' \
                       or from_split[0] == '-'
                if is_num(from_split[0]):
                    to_split[0] = str(int(to_split[0]) + int(from_split[0]))

            to_lines[i] = ":".join(to_split)

    with open(to_gcov, 'w') as to_file:
        to_file.writelines(to_lines)


def process_tu(tu_path, output):
    '''Process a directory containing files originated from checking a tu.'''
    for root, _, files in os.walk(tu_path):
        for gcovfile in files:
            _, ext = os.path.splitext(gcovfile)
            if ext != ".gcov":
                continue
            gcov_in_path = os.path.join(root, gcovfile)
            gcov_out_path = os.path.join(
                    output, os.path.relpath(gcov_in_path, tu_path))
            if os.path.exists(gcov_out_path):
                merge_gcov(gcov_in_path, gcov_out_path)
            else:
                # No merging needed.
                try:
                    os.makedirs(os.sep.join(gcov_out_path.split(os.sep)[:-1]))
                except OSError:
                    pass
                shutil.copyfile(gcov_in_path, gcov_out_path)


def main(indir, outdir):
    '''Process each tu dir.'''
    if os.path.exists(outdir):
        shutil.rmtree(outdir)
    os.mkdir(outdir)

    for tu_dir in os.listdir(indir):
        tu_path = os.path.join(indir, tu_dir)
        if not os.path.isdir(tu_path):
            continue

        process_tu(tu_path, outdir)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Merge gcov files from "
                                     "different translation units")
    parser.add_argument("--input", "-i", help="Directory containing the input"
                        " gcov files", required=True)
    parser.add_argument("--output", "-o", help="Output directory for gcov"
                        " files. Warning! Output tree will be cleared!",
                        required=True)
    args = parser.parse_args()
    main(args.input, args.output)
