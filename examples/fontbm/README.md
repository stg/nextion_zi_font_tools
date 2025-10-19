##  Example for FontBM command line bitmap font generator

This example produces a 32 pixels variant of Atkinson Hyperlegible Regular.  

See FontBM repository https://github.com/vladimirgamalyan/  
Or get the binaries here https://github.com/vladimirgamalyan/fontbm/releases  

- Run ```fontbm.exe --font-file c:\temp\AtkinsonHyperlegible-Regular.ttf --font-size 27 --chars 32-255 --data-format bin --output Hyperlegible```
  to produce Hyperlegible.fnt and Hyperlegible_0.png  
- Run  ```bmf_to_zi.exe Hyperlegible```
  to produce Hyperlegible.zi

