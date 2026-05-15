| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| `build/cracker_bench --impl tinyptr --hash 1637ff9c1826eb09071d234ea6b5563b --threads 4` | 9.226 ± 0.518 | 8.771 | 10.021 | 1.00 |
| `build/cracker_bench --impl naive   --hash 1637ff9c1826eb09071d234ea6b5563b --threads 4` | 10.317 ± 0.266 | 9.983 | 10.596 | 1.12 ± 0.07 |
| `build/cracker_bench --impl stdmap  --hash 1637ff9c1826eb09071d234ea6b5563b --threads 4` | 17.177 ± 0.272 | 16.715 | 17.375 | 1.86 ± 0.11 |
| `build/cracker_bench --impl prob    --hash 1637ff9c1826eb09071d234ea6b5563b --threads 4` | 11.501 ± 0.521 | 11.038 | 12.355 | 1.25 ± 0.09 |
