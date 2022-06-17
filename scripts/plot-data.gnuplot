plot for [i=1:words(files)] sprintf('< sort -n -k 2 %s', word(files, i)) using 2 w points title word(files, i) noenhance
