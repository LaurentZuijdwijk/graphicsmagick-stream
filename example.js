var gm = require('./')
var fs = require('fs')

var convert = gm()

fs.createReadStream('test.jpg')
  .pipe(convert({scale: {width: 800, height: 800}, quality: 30}))
  .pipe(fs.createWriteStream('output.jpg'))

fs.createReadStream('test.jpg')
  .pipe(convert({scale: 300, rotate: 180, format: 'png'}))
  .pipe(fs.createWriteStream('out1.png'))
