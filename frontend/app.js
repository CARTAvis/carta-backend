namespace = "/test";




const COLOR_MAPS_ALL = ["Accent", "afmhot", "autumn", "binary", "Blues", "bone", "BrBG", "brg", "BuGn", "BuPu", "bwr", "CMRmap", "cool", "coolwarm",
    "copper", "cubehelix", "Dark2", "flag", "gist_earth", "gist_gray", "gist_heat", "gist_ncar", "gist_rainbow", "gist_stern", "gist_yarg",
    "GnBu", "gnuplot", "gnuplot2", "gray", "Greens", "Greys", "hot", "hsv", "inferno", "jet", "magma", "nipy_spectral", "ocean", "Oranges",
    "OrRd", "Paired", "Pastel1", "Pastel2", "pink", "PiYG", "plasma", "PRGn", "prism", "PuBu", "PuBuGn", "PuOr", "PuRd", "Purples", "rainbow",
    "RdBu", "RdGy", "RdPu", "RdYlBu", "RdYlGn", "Reds", "seismic", "Set1", "Set2", "Set3", "Spectral", "spring", "summer", "tab10", "tab20",
    "tab20b", "tab20c", "terrain", "viridis", "winter", "Wistia", "YlGn", "YlGnBu", "YlOrBr", "YlOrRd"];


var gl;
var extTextureFloat;
var texture;

function initGL(canvasGL) {
    try {
        gl = canvasGL.getContext("webgl");
        if (!gl) {
            return;
        }
        gl.viewportWidth = canvasGL.width;
        gl.viewportHeight = canvasGL.height;
    } catch (e) {
        console.log(e);
    }
    if (!gl) {
        alert("Could not initialise WebGL");
    }

    extTextureFloat = gl.getExtension("OES_texture_float");

    if (!extTextureFloat) {
        alert("Could not initialise WebGL extensions");
    }
}

function getShader(gl, id) {
    var shaderScript = document.getElementById(id);
    if (!shaderScript) {
        return null;
    }

    var str = "";
    var k = shaderScript.firstChild;
    while (k) {
        if (k.nodeType == 3) {
            str += k.textContent;
        }
        k = k.nextSibling;
    }

    var shader;
    if (shaderScript.type == "x-shader/x-fragment") {
        shader = gl.createShader(gl.FRAGMENT_SHADER);
    } else if (shaderScript.type == "x-shader/x-vertex") {
        shader = gl.createShader(gl.VERTEX_SHADER);
    } else {
        return null;
    }

    gl.shaderSource(shader, str);
    gl.compileShader(shader);

    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        alert(gl.getShaderInfoLog(shader));
        return null;
    }

    return shader;
}

var shaderProgram;

function initShaders() {
    var fragmentShader = getShader(gl, extTextureFloat ? "cmap-float" : "cmap-rgba");
    var vertexShader = getShader(gl, "vs");

    shaderProgram = gl.createProgram();
    gl.attachShader(shaderProgram, vertexShader);
    gl.attachShader(shaderProgram, fragmentShader);
    gl.linkProgram(shaderProgram);

    if (!gl.getProgramParameter(shaderProgram, gl.LINK_STATUS)) {
        alert("Could not initialise shaders");
    }

    gl.useProgram(shaderProgram);

    shaderProgram.vertexPositionAttribute = gl.getAttribLocation(shaderProgram, "aVertexPosition");
    gl.enableVertexAttribArray(shaderProgram.vertexPositionAttribute);

    shaderProgram.vertexUVAttribute = gl.getAttribLocation(shaderProgram, "aVertexUV");
    gl.enableVertexAttribArray(shaderProgram.vertexUVAttribute);

    shaderProgram.pMatrixUniform = gl.getUniformLocation(shaderProgram, "uPMatrix");
    shaderProgram.mvMatrixUniform = gl.getUniformLocation(shaderProgram, "uMVMatrix");

    shaderProgram.MinValUniform = gl.getUniformLocation(shaderProgram, "uMinVal");
    shaderProgram.MaxValUniform = gl.getUniformLocation(shaderProgram, "uMaxVal");
    shaderProgram.MinColorUniform = gl.getUniformLocation(shaderProgram, "uMinCol");
    shaderProgram.MaxColorUniform = gl.getUniformLocation(shaderProgram, "uMaxCol");

    shaderProgram.ViewportSizeUniform = gl.getUniformLocation(shaderProgram, "uViewportSize");

    shaderProgram.DataTexture = gl.getUniformLocation(shaderProgram, "uDataTexture");
    shaderProgram.CmapTexture = gl.getUniformLocation(shaderProgram, "uCmapTexture");
    shaderProgram.NumCmaps = gl.getUniformLocation(shaderProgram, "uNumCmaps");
    shaderProgram.CmapIndex = gl.getUniformLocation(shaderProgram, "uCmapIndex");
    gl.uniform1i(shaderProgram.DataTexture, 0);
    gl.uniform1i(shaderProgram.CmapTexture, 1);
    gl.uniform1i(shaderProgram.NumCmaps, 79);
    gl.uniform1i(shaderProgram.CmapIndex, 41);
}


function setUniforms() {
    gl.uniform2f(
        shaderProgram.ViewportSizeUniform,
        gl.viewportWidth,
        gl.viewportHeight
    );
}


var squareVertexPositionBuffer;
var squareVertexUVBuffer;

function initBuffers() {
    //Create Square Position Buffer
    squareVertexPositionBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexPositionBuffer);
    var vertices = [
        -1.0, -1.0, 0.0,
        1.0, -1.0, 0.0,
        -1.0, 1.0, 0.0,
        1.0, 1.0, 0.0
    ];

    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.STATIC_DRAW);
    squareVertexPositionBuffer.itemSize = 3;
    squareVertexPositionBuffer.numItems = 4;

    //Create Square UV Buffer
    squareVertexUVBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexUVBuffer);
    var uvs = [
        0.0, 0.0,
        1.0, 0.0,
        0.0, 1.0,
        1.0, 1.0
    ];

    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(uvs), gl.STATIC_DRAW);
    squareVertexUVBuffer.itemSize = 2;
    squareVertexUVBuffer.numItems = 4;
}


function drawScene() {
    gl.viewport(0, 0, gl.viewportWidth, gl.viewportHeight);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexPositionBuffer);
    gl.vertexAttribPointer(shaderProgram.vertexPositionAttribute, squareVertexPositionBuffer.itemSize, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexUVBuffer);
    gl.vertexAttribPointer(shaderProgram.vertexUVAttribute, squareVertexUVBuffer.itemSize, gl.FLOAT, false, 0, 0);
    gl.uniform2f(shaderProgram.ViewportSizeUniform, gl.viewportWidth, gl.viewportHeight);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, squareVertexPositionBuffer.numItems);
}

function loadImageTexture(gl, url) {
    const imageTexture = gl.createTexture();
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, imageTexture);

    // Because images have to be download over the internet
    // they might take a moment until they are ready.
    // Until then put a single pixel in the texture so we can
    // use it immediately. When the image has finished downloading
    // we"ll update the texture with the contents of the image.
    const level = 0;
    const internalFormat = gl.RGB;
    const width = 1;
    const height = 1;
    const border = 0;
    const srcFormat = gl.RGB;
    const srcType = gl.UNSIGNED_BYTE;
    const pixel = new Uint8Array([0, 0, 255]);  // opaque blue
    gl.texImage2D(gl.TEXTURE_2D, level, internalFormat,
        width, height, border, srcFormat, srcType,
        pixel);

    const image = new Image();
    image.onload = function () {
        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, imageTexture);
        gl.texImage2D(gl.TEXTURE_2D, level, internalFormat,
            srcFormat, srcType, image);

        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    };
    image.src = url;
    return imageTexture;
}


function loadFP32Texture(data, width, height) {
    // Create a texture.
    texture = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texture);

    // fill texture with 3x2 pixels
    const level = 0;
    const internalFormat = gl.LUMINANCE;
    const border = 0;
    const format = gl.LUMINANCE;
    const type = gl.FLOAT;

    const alignment = 1;
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, alignment);
    gl.texImage2D(gl.TEXTURE_2D, level, internalFormat, width, height, border,
        format, type, data);

    // set the filtering so we don"t need mips and it"s not filtered
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

function loadRGBATexture(data, width, height) {
    // Create a texture.
    var texture = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texture);

    const level = 0;
    const internalFormat = gl.RGBA;
    const border = 0;
    const format = gl.RGBA;
    const type = gl.UNSIGNED_BYTE;

    const alignment = 1;
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, alignment);
    gl.texImage2D(gl.TEXTURE_2D, level, internalFormat, width, height, border,
        format, type, data);

    // set the filtering so we don"t need mips and it"s not filtered
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

function getGLCoords(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel) {
    var topLeft = {
        x: imageCenter.x - canvasSize.x / (2.0 * zoomLevel),
        y: imageCenter.y - canvasSize.y / (2.0 * zoomLevel)
    };

    var bottomRight = {
        x: imageCenter.x + canvasSize.x / (2.0 * zoomLevel),
        y: imageCenter.y + canvasSize.y / (2.0 * zoomLevel)
    };

    var topLeftGL = {
        x: -1 + (currentRegion.x - topLeft.x) / (imageCenter.x - topLeft.x),
        y: -1 + (currentRegion.y - topLeft.y) / (imageCenter.y - topLeft.y)
    };

    var bottomRightGL = {
        x: (currentRegion.x + currentRegion.w - imageCenter.x) / (bottomRight.x - imageCenter.x),
        y: (currentRegion.y + currentRegion.h - imageCenter.y) / (bottomRight.y - imageCenter.y)
    };

    return [
        topLeftGL.x, topLeftGL.y, 0.0,
        bottomRightGL.x, topLeftGL.y, 0.0,
        topLeftGL.x, bottomRightGL.y, 0.0,
        bottomRightGL.x, bottomRightGL.y, 0.0
    ];

}


function updateVertices(vertices) {
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexPositionBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.STATIC_DRAW);
}


function calculateMip(zoomLevel) {
    var mipExact = 1.0 / zoomLevel;
    mipExact = Math.max(1.0, mipExact);
    var newMip = parseInt(mipExact % 1.0 < 0.25 ? Math.floor(mipExact) : Math.ceil(mipExact));
    return newMip;
}


$(document).ready(function () {
    connection = new WebSocket(`ws://${window.location.hostname}:3002`);
    connection.binaryType = "arraybuffer";

    var overlayCanvas = document.getElementById("overlay");
    var overlay = overlayCanvas.getContext("2d");
    var canvasGL = document.getElementById("webgl");
    initGL(canvasGL);
    initShaders();
    initBuffers();
    var colorMapTexture = loadImageTexture(gl, "allmaps.png");
    gl.clearColor(0.0, 0.0, 0.0, 1.0);
    gl.enable(gl.DEPTH_TEST);

    var minVal = -1;
    var maxVal = 1;
    var regionImageData = null;

    // decompression buffers
    var dataPtr = null;
    var dataHeap = null;
    var nDataBytes = 0;
    var nDataBytesCompressed = 0;
    var dataPtrUint = null;
    var dataHeapUint = null;
    var resultFloat = null;


    var imageCenter = {
        x: 0,
        y: 0
    };

    var imageSize = {
        x: 0,
        y: 0
    };

    var bounds = {
        x: 0,
        y: 0,
        w: 0,
        h: 0
    };

    var canvasSize = {
        x: canvasGL.width,
        y: canvasGL.height
    };

    var currentRegion = {
        x: 0,
        y: 0,
        w: 0,
        h: 0,
        mip: 0,
        band: 0,
        compression: 0
    };


    //zooming
    var zoomLevel = Math.min(canvasSize.x / imageSize.x, canvasSize.y / imageSize.y);

    // cursor
    var cursorPos = {x: 0, y: 0};

    var requiredRegion = {
        x: 0,
        y: 0,
        w: imageSize.x,
        h: imageSize.y,
        mip: calculateMip(zoomLevel),
        band: 0,
        compression: 12
    };

    $("#zoomLevel").val(zoomLevel);
    $("#current_view_mip").val(0);
    $("#req_view_x").html(requiredRegion.x);
    $("#req_view_y").html(requiredRegion.y);
    $("#req_view_w").html(requiredRegion.w);
    $("#req_view_h").html(requiredRegion.h);
    $("#req_view_mip").html(requiredRegion.mip);

    // Selects
    var select = $("#cmap_select");
    for (var i = 0; i < COLOR_MAPS_ALL.length; i++) {
        select.append(new Option(COLOR_MAPS_ALL[i], i))
    }
    $("#cmap_select").val(COLOR_MAPS_ALL.indexOf("magma"));


    //dragging
    var dragStarted = false;
    var isDragging = false;
    var previousDragLocation = null;
    var isTouchZooming = false;
    var previousZoomSeparation = 0;
    var isZoomingToRegion = false;
    var initialZoomToRegionPos = null;
    var frozenCursor = false;

    var scrollTimeout = null;

    // updating min/max from histogram
    var bandStats = null;


    var histogram = new Chart(document.getElementById("histogram").getContext("2d"), {
        type: "line",
        data: {
            datasets: [{
                data: [],
                label: null,
                pointRadius: 0,
                fill: false,
                borderColor: "blue",
                borderWidth: 1,
                borderJoinStyle: "miter",
                steppedLine: true
            }]
        },
        options: {
            events: ['mousemove', 'mousedown', 'mouseup'],
            zoomBox:{
                rescale: false,
                mode: 'x'
            },
            annotation: {
                annotations: [
                    {
                        id: "histline",
                        type: "line",
                        mode: "vertical",
                        scaleID: "x-axis-0",
                        borderColor: "red",
                        borderWidth: 1,
                    },
                    {
                        id: "clampmin",
                        type: "line",
                        mode: "vertical",
                        scaleID: "x-axis-0",
                        borderColor: "grey",
                        borderWidth: 1,
                        borderDash: [10]
                    },
                    {
                        id: "clampmax",
                        type: "line",
                        mode: "vertical",
                        scaleID: "x-axis-0",
                        borderColor: "grey",
                        borderWidth: 1,
                        borderDash: [10]
                    }
                ]
            },
            responsive: true,
            legend: {display: false},
            scales: {
                xAxes: [{
                    type: "linear",
                    position: "bottom",
                    offset: "false",
                    scaleLabel: {
                        display: true,
                        labelString: "Value"
                    },

                }],
                yAxes: [{
                    type: "logarithmic",
                    offset: "false",
                    scaleLabel: {
                        display: true,
                        labelString: "Count"
                    }
                }]
            },
            customLine: {
                color: 'black'
            },
            animation: {
                duration: 0
            },
            hover: {
                animationDuration: 0
            },
            responsiveAnimationDuration: 0,
            elements: {
                line: {
                    tension: 0
                }
            }
        }
    });

    var profileX = new Chart(document.getElementById("profileX").getContext("2d"), {
        type: "line",
        data: {
            datasets: [{
                data: [],
                pointRadius: 0,
                pointHoverRadius: 10,
                fill: false,
                borderColor: "blue",
                borderWidth: 1,
                borderJoinStyle: "miter",
                steppedLine: true
            }]
        },
        options: {
            events: ['mousemove', 'mousedown', 'mouseup'],
            annotation: {
                annotations: [
                    {
                        id: "xline",
                        type: "line",
                        mode: "vertical",
                        scaleID: "x-axis-0",
                        borderColor: "red",
                        borderWidth: 1,
                    },
                    {
                        id: "mean",
                        type: "line",
                        mode: "horizontal",
                        scaleID: "y-axis-0",
                        borderColor: "grey",
                        borderWidth: 1,
                        borderDash: [5]
                    }
                ]
            },
            legend: {display: false},
            scales: {
                xAxes: [{
                    type: "linear",
                    position: "bottom",
                    scaleLabel: {
                        display: true,
                        labelString: "Pixel X Coordinate"
                    },
                    ticks: {
                        maxTicksLimit: 9
                    }

                }],
                yAxes: [{
                    type: "linear",
                    scaleLabel: {
                        display: true,
                        labelString: "Value"
                    }
                }]
            },
            animation: {
                duration: 0
            },
            hover: {
                animationDuration: 0
            },
            responsiveAnimationDuration: 0,
            elements: {
                line: {
                    tension: 0
                }
            }
        }
    });

    var profileY = new Chart(document.getElementById("profileY").getContext("2d"), {
        type: "line",
        data: {
            datasets: [{
                data: [],
                label: null,
                pointRadius: 0,
                fill: false,
                borderColor: "blue",
                borderWidth: 1,
                borderJoinStyle: "miter",
                steppedLine: true
            }]
        },
        options: {
            events: ['mousemove', 'mousedown', 'mouseup'],
            annotation: {
                annotations: [
                    {
                        id: "yline",
                        type: "line",
                        mode: "vertical",
                        scaleID: "x-axis-0",
                        borderColor: "red",
                        borderWidth: 1
                    },
                    {
                        id: "mean",
                        type: "line",
                        mode: "horizontal",
                        scaleID: "y-axis-0",
                        borderColor: "grey",
                        borderWidth: 1,
                        borderDash: [5]
                    }
                ]
            },
            legend: {display: false},
            scales: {
                xAxes: [{
                    type: "linear",
                    position: "bottom",
                    scaleLabel: {
                        display: true,
                        labelString: "Pixel Y Coordinate"
                    },
                    ticks: {
                        maxTicksLimit: 9
                    }
                }],
                yAxes: [{
                    type: "linear",
                    scaleLabel: {
                        display: true,
                        labelString: "Value"
                    }
                }]
            },
            animation: {
                duration: 0
            },
            hover: {
                animationDuration: 0
            },
            responsiveAnimationDuration: 0,
            elements: {
                line: {
                    tension: 0
                }
            }
        }
    });

    function updateBounds(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel) {
        var topLeft = {
            x: imageCenter.x - canvasSize.x / (2.0 * zoomLevel),
            y: imageCenter.y - canvasSize.y / (2.0 * zoomLevel)
        };

        var bottomRight = {
            x: imageCenter.x + canvasSize.x / (2.0 * zoomLevel),
            y: imageCenter.y + canvasSize.y / (2.0 * zoomLevel)
        };

        bounds = {
            x: Math.max(topLeft.x, 0),
            y: Math.max(topLeft.y, 0),
            w: Math.min(bottomRight.x, imageSize.x) - Math.max(topLeft.x, 0),
            h: Math.min(bottomRight.y, imageSize.y) - Math.max(topLeft.y, 0)
        };

        $("#req_view_x").html(bounds.x);
        $("#req_view_y").html(bounds.y);
        $("#req_view_w").html(bounds.w);
        $("#req_view_h").html(bounds.h);
    }

    function checkAndUpdateRegion() {
        if (connection) {

            var requiresUpdate = false;
            var requestedRegion = {
                band: parseInt($("#band_val").val()),
                x: parseInt($("#req_view_x").html()),
                y: parseInt($("#req_view_y").html()),
                w: parseInt($("#req_view_w").html()),
                h: parseInt($("#req_view_h").html()),
                mip: parseInt($("#req_view_mip").html()),
                compression: parseInt($("#compression_val").val())
            };

            requestedRegion.x = Math.floor(requestedRegion.x);
            requestedRegion.y = Math.floor(requestedRegion.y);
            requestedRegion.w = Math.floor(requestedRegion.w / requestedRegion.mip) * requestedRegion.mip;
            requestedRegion.h = Math.floor(requestedRegion.h / requestedRegion.mip) * requestedRegion.mip;


            // All requests with different compression settings, a band change or a higher-res MIP need to be sent through
            if (currentRegion.mip === 0 || requestedRegion.compression !== currentRegion.compression || requestedRegion.band !== currentRegion.band || requestedRegion.mip < currentRegion.mip) {
                requiresUpdate = true;
            }
            else {
                // Check XY bounnds. If requested region is a sub-region of the existing region, no need to do anything.
                if (requestedRegion.x < currentRegion.x || requestedRegion.x + requestedRegion.w > currentRegion.x + currentRegion.w)
                    requiresUpdate = true;
                else if (requestedRegion.y < currentRegion.y || requestedRegion.y + requestedRegion.h > currentRegion.y + currentRegion.h)
                    requiresUpdate = true;
            }

            if (requiresUpdate) {
                var payload = {
                    event: "region_read",
                    message: {
                        band: requestedRegion.band,
                        x: requestedRegion.x,
                        y: requestedRegion.y,
                        w: requestedRegion.w,
                        h: requestedRegion.h,
                        mip: requestedRegion.mip,
                        compression: requestedRegion.compression
                    }
                };
                // For latency emulation:
                // setTimeout(function () {
                //     connection.send(JSON.stringify(payload));
                // }, 500);
                connection.send(JSON.stringify(payload));
            }
            else {

            }
        }
    }

    $("#compression_val").on("input", checkAndUpdateRegion);
    $("#band_val").on("input", $.debounce(16, checkAndUpdateRegion));

    $("#min_val").on("input", $.debounce(16, function () {
        $("#percentile_select").val("custom");
        minVal = this.value;
        histogram.annotation.options.annotations[1].value = minVal;
        histogram.update({duration: 0});
        $("#min_val_label").text(minVal);
        refreshColorScheme();
    }));

    $("#max_val").on("input", $.debounce(16, function () {
        $("#percentile_select").val("custom");
        maxVal = this.value;
        $("#max_val_label").text(maxVal);
        histogram.annotation.options.annotations[2].value = maxVal;
        histogram.update({duration: 0});
        refreshColorScheme();
    }));

    $("#cmap_select").on("change", $.debounce(16, () => {
        refreshColorScheme();

    }));

    $("#percentile_select").on("change", $.debounce(16, () => {
        updateSliders(bandStats);

    }));

    function drawCursor(pos, crossWidth) {
        overlay.strokeStyle = "#0000BB";
        overlay.beginPath();
        overlay.moveTo(pos.x, pos.y - crossWidth);
        overlay.lineTo(pos.x, pos.y + crossWidth);
        overlay.moveTo(pos.x + crossWidth, pos.y);
        overlay.lineTo(pos.x - crossWidth, pos.y);
        overlay.stroke();
    }

    function updateProfilesAndCursor(pos, updateCursor = true) {

        if (updateCursor) {
            cursorPos = pos;
            var crossWidth = 15;
            overlay.clearRect(0, 0, canvasSize.x, canvasSize.y);
            drawCursor(cursorPos, crossWidth);
        }

        var imageCoords = getImageCoords(cursorPos);
        var zVal = getCursorValue(imageCoords);

        requestAnimationFrame(() => {
            var xProfileInfo = getXProfile(imageCoords);
            if (xProfileInfo && xProfileInfo.data) {
                profileX.annotation.options.annotations[0].value = imageCoords.x;
                profileX.annotation.options.annotations[1].value = xProfileInfo.mean;
                profileX.data.datasets[0].data.length = xProfileInfo.data.length;
                for (var i = 0; i < xProfileInfo.data.length; i++) {
                    profileX.data.datasets[0].data[i] = {x: xProfileInfo.coords[i], y: xProfileInfo.data[i]}
                }
                profileX.options.scales.xAxes[0].ticks.min = xProfileInfo.coords[0];
                profileX.options.scales.xAxes[0].ticks.max = xProfileInfo.coords[xProfileInfo.data.length - 1];
                // Automatically scale y axis again and remove saved zoom setting
                profileX.options.scales.yAxes[0].ticks.min = undefined;
                profileX.options.scales.yAxes[0].ticks.max = undefined;
                profileX.zoomBox.originalZoom=null;

                profileX.update({duration: 0});
            }

            var yProfileInfo = getYProfile(imageCoords);
            if (yProfileInfo && yProfileInfo.data) {
                profileY.annotation.options.annotations[0].value = imageCoords.y;
                profileY.annotation.options.annotations[1].value = yProfileInfo.mean;
                profileY.data.datasets[0].data.length = yProfileInfo.data.length;
                for (var i = 0; i < yProfileInfo.data.length; i++) {
                    profileY.data.datasets[0].data[i] = {x: yProfileInfo.coords[i], y: yProfileInfo.data[i]}
                }
                profileY.options.scales.xAxes[0].ticks.min = yProfileInfo.coords[0];
                profileY.options.scales.xAxes[0].ticks.max = yProfileInfo.coords[yProfileInfo.data.length - 1];
                // Automatically scale y axis again and remove saved zoom setting
                profileY.options.scales.yAxes[0].ticks.min = undefined;
                profileY.options.scales.yAxes[0].ticks.max = undefined;
                profileY.zoomBox.originalZoom=null;

                profileY.update({duration: 0});
            }

            if (!isNaN(zVal)) {
                histogram.annotation.options.annotations[0].value = zVal;
                histogram.update({duration: 0});
            }
        });

        //var dataPos = {x: Math.floor(mousePos.x / regionImageData.mip), y: regionImageData.h - Math.floor(mousePos.y / regionImageData.mip)};
        //var zVal = regionImageData.fp32payload[dataPos.y * regionImageData.w + dataPos.x];
        var cursorInfo = `(${imageCoords.x.toFixed(2)}, ${imageCoords.y.toFixed(2)}): ${zVal !== undefined ? zVal.toFixed(5) : "NaN"}`;
        $("#cursor").html(cursorInfo);
    }

    $("#overlay").on("mousemove", $.debounce(5, (event) => {
        if (!regionImageData)
            return;
        var mousePos = getMousePos(canvasGL, event);

        if (isZoomingToRegion && initialZoomToRegionPos) {
            var width = mousePos.x - initialZoomToRegionPos.x;
            var height = mousePos.y - initialZoomToRegionPos.y;
            overlay.clearRect(0, 0, canvasSize.x, canvasSize.y);
            overlay.strokeStyle = "#FF0000";
            overlay.strokeRect(initialZoomToRegionPos.x, initialZoomToRegionPos.y, width, height);
        }
        else if (isDragging) {

            if (dragStarted) {
                dragStarted = false;
                previousDragLocation = mousePos;
            }
            else {
                imageCenter.x -= (mousePos.x - previousDragLocation.x) / zoomLevel;
                imageCenter.y += (mousePos.y - previousDragLocation.y) / zoomLevel;
                updateBounds(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);
                var vertices = getGLCoords(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);
                updateVertices(vertices);
                refreshColorScheme();
                previousDragLocation = mousePos;
                $("#centerX").val(imageCenter.x);
                $("#centerY").val(imageCenter.y);
            }
            updateProfilesAndCursor(mousePos);
        }
        else if (!frozenCursor)
            updateProfilesAndCursor(mousePos);
    }));

    $(document).keydown((event) => {
        if (event.which == 32) {
            event.preventDefault();
            frozenCursor = !frozenCursor;
        }

    });

    $("#overlay").on("touchstart", () => {
        isDragging = true;
        dragStarted = true;
    });

    $("#overlay").on("touchend", () => {
        isDragging = false;
        dragStarted = false;
        isTouchZooming = false;
        checkAndUpdateRegion();
    });

    $("#overlay").on("touchmove", (event) => {
        if (!regionImageData)
            return;
        event.preventDefault();

        // Pinch-zoom
        if (event.originalEvent.touches.length == 2) {
            var previousZoomLevel = zoomLevel;
            //var mousePos = getMousePos(canvasGL, evt);
            var touchA = getMousePos(canvasGL, event.originalEvent.touches[0]);
            var touchB = getMousePos(canvasGL, event.originalEvent.touches[1]);
            var zoomSeparation = Math.sqrt(Math.pow(touchA.x - touchB.x, 2) + Math.pow(touchA.y - touchB.y, 2));
            var touchCenter = {x: (touchA.x + touchB.x) / 2.0, y: (touchA.y + touchB.y) / 2.0};
            updateProfilesAndCursor(touchCenter);
            var imageCoords = getImageCoords(touchCenter);
            if (!isTouchZooming) {
                isTouchZooming = true;
                previousZoomSeparation = zoomSeparation;
            }
            else {
                zoomLevel = zoomLevel * zoomSeparation / previousZoomSeparation;
                previousZoomSeparation = zoomSeparation;

                imageCenter = {
                    x: imageCoords.x + previousZoomLevel / zoomLevel * (imageCenter.x - imageCoords.x),
                    y: imageCoords.y + previousZoomLevel / zoomLevel * (imageCenter.y - imageCoords.y)
                };
                updateZoom(zoomLevel, true);
            }


        }
        else if (event.originalEvent.touches.length == 1) {
            //Dragging
            var mousePos = getMousePos(canvasGL, event.originalEvent.touches[0]);
            updateProfilesAndCursor(mousePos);

            if (isDragging) {

                if (dragStarted) {
                    dragStarted = false;
                    previousDragLocation = mousePos;
                }
                else {
                    imageCenter.x -= (mousePos.x - previousDragLocation.x) / zoomLevel;
                    imageCenter.y += (mousePos.y - previousDragLocation.y) / zoomLevel;
                    updateBounds(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);
                    var vertices = getGLCoords(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);
                    updateVertices(vertices);
                    refreshColorScheme();
                    previousDragLocation = mousePos;
                    $("#centerX").val(imageCenter.x);
                    $("#centerY").val(imageCenter.y);
                }
            }

        }
    });

    function getImageCoords(M) {
        return {
            x: imageCenter.x + (M.x - canvasSize.x / 2.0) / zoomLevel,
            y: imageCenter.y - (M.y - canvasSize.y / 2.0) / zoomLevel
        }
    }

    function getRegionCoords(P) {
        return {
            x: Math.floor((P.x - currentRegion.x) / currentRegion.mip),
            y: Math.floor((P.y - currentRegion.y) / currentRegion.mip)
        }
    }

    function getCursorValue(P) {
        if (P.x < 0 || P.x >= imageSize.x || P.y < 0 || P.y >= imageSize.y)
            return undefined;
        else {
            var regionCoords = getRegionCoords(P);
            return regionImageData.fp32payload[regionCoords.y * regionImageData.w + regionCoords.x];
        }
    }

    function getXProfile(P) {
        if (P.x < 0 || P.x >= imageSize.x || P.y < 0 || P.y >= imageSize.y)
            return undefined;
        else {
            var regionCoords = getRegionCoords(P);
            var startX = getRegionCoords({x: bounds.x, y: 0}).x;
            var endX = getRegionCoords({x: bounds.x + bounds.w - 1, y: 0}).x;


            if (endX - startX <= 0)
                return undefined;

            var coords = new Array(endX - startX);
            var data = new Array(endX - startX);

            var mean = 0;
            var minVal = Number.MAX_VALUE;
            var maxVal = -Number.MAX_VALUE;
            var countValid = 0;
            for (var i = 0; i < coords.length; i++) {
                var x = i + startX;
                var val = regionImageData.fp32payload[regionCoords.y * regionImageData.w + x];
                data[i] = val;
                coords[i] = (currentRegion.x + currentRegion.mip * x);

                if (!isNaN(val)) {
                    minVal = Math.min(minVal, val);
                    maxVal = Math.max(maxVal, val);
                    mean += val;
                    countValid++;
                }
            }

            mean /= Math.max(countValid, 1);

            return {
                currentVal: regionImageData.fp32payload[regionCoords.y * regionImageData.w + regionCoords.x],
                data,
                coords,
                mean,
                minVal,
                maxVal
            };
        }
    }

    function getXProfileCanvasJS(P) {
        if (P.x < 0 || P.x >= imageSize.x || P.y < 0 || P.y >= imageSize.y)
            return undefined;
        else {
            var regionCoords = getRegionCoords(P);
            var startX = getRegionCoords({x: bounds.x, y: 0}).x;
            var endX = getRegionCoords({x: bounds.x + bounds.w - 1, y: 0}).x;


            if (endX - startX <= 0)
                return undefined;

            var data = new Array(endX - startX);

            var mean = 0;
            var minVal = Number.MAX_VALUE;
            var maxVal = -Number.MAX_VALUE;
            var countValid = 0;
            for (var i = 0; i < data.length; i++) {
                var x = i + startX;
                var val = regionImageData.fp32payload[regionCoords.y * regionImageData.w + x];
                data[i] = {x: (currentRegion.x + currentRegion.mip * x), y: val};

                if (!isNaN(val)) {
                    minVal = Math.min(minVal, val);
                    maxVal = Math.max(maxVal, val);
                    mean += val;
                    countValid++;
                }
            }

            mean /= Math.max(countValid, 1);

            return {
                currentVal: regionImageData.fp32payload[regionCoords.y * regionImageData.w + regionCoords.x],
                data,
                mean,
                minVal,
                maxVal
            };
        }
    }

    function getYProfile(P) {
        if (P.x < 0 || P.x >= imageSize.x || P.y < 0 || P.y >= imageSize.y)
            return undefined;
        else {
            var regionCoords = getRegionCoords(P);
            var startY = getRegionCoords({y: bounds.y, x: 0}).y;
            var endY = getRegionCoords({y: bounds.y + bounds.h - 1, x: 0}).y;
            var data = new Array(endY - startY);
            var coords = new Array(endY - startY);
            var mean = 0;
            var minVal = Number.MAX_VALUE;
            var maxVal = -Number.MAX_VALUE;
            var countValid = 0;
            for (var i = 0; i < data.length; i++) {
                var y = i + startY;
                var val = regionImageData.fp32payload[y * regionImageData.w + regionCoords.x];
                data[i] = val;
                coords[i] = (currentRegion.y + currentRegion.mip * y);
                if (!isNaN(val)) {
                    minVal = Math.min(minVal, val);
                    maxVal = Math.max(maxVal, val);
                    mean += val;
                    countValid++;
                }
            }

            mean /= Math.max(countValid, 1);

            return {
                currentVal: regionImageData.fp32payload[regionCoords.y * regionImageData.w + regionCoords.x],
                data,
                coords,
                mean,
                minVal,
                maxVal
            };
        }
    }

    function getYProfileCanvasJS(P) {
        if (P.x < 0 || P.x >= imageSize.x || P.y < 0 || P.y >= imageSize.y)
            return undefined;
        else {
            var regionCoords = getRegionCoords(P);
            var startY = getRegionCoords({y: bounds.y, x: 0}).y;
            var endY = getRegionCoords({y: bounds.y + bounds.h - 1, x: 0}).y;
            var data = new Array(endY - startY);
            var mean = 0;
            var minVal = Number.MAX_VALUE;
            var maxVal = -Number.MAX_VALUE;
            var countValid = 0;
            for (var i = 0; i < data.length; i++) {
                var y = i + startY;
                var val = regionImageData.fp32payload[y * regionImageData.w + regionCoords.x];
                data[i] = {x: val, y: currentRegion.y + currentRegion.mip * y};
                if (!isNaN(val)) {
                    minVal = Math.min(minVal, val);
                    maxVal = Math.max(maxVal, val);
                    mean += val;
                    countValid++;
                }
            }

            mean /= Math.max(countValid, 1);

            return {
                currentVal: regionImageData.fp32payload[regionCoords.y * regionImageData.w + regionCoords.x],
                data,
                mean,
                minVal,
                maxVal
            };
        }
    }

    function updateZoom(newZoomLevel, doTimeout) {
        zoomLevel = Math.max(newZoomLevel, 1e-6);
        $("#zoomLevel").val(zoomLevel);
        $("#centerX").val(imageCenter.x);
        $("#centerY").val(imageCenter.y);

        var newMip = calculateMip(zoomLevel);
        var currentMip = currentRegion.mip;
        $("#req_view_mip").html(newMip);

        updateBounds(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);

        var vertices = getGLCoords(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);
        updateVertices(vertices);
        refreshColorScheme();

        if (doTimeout) {
            if (scrollTimeout)
                clearTimeout(scrollTimeout);
            scrollTimeout = setTimeout(checkAndUpdateRegion, 200);
        }
        else
            checkAndUpdateRegion();
    }

    $("#overlay").on("mousewheel", (event) => {
        event.preventDefault();
        var delta = event.originalEvent.wheelDelta;
        var previousZoomLevel = zoomLevel;
        var zoomSpeed = 1.1;
        zoomLevel = zoomLevel * (delta > 0 ? zoomSpeed : 1.0 / zoomSpeed);
        var mousePos = getMousePos(canvasGL, event);
        var imageCoords = getImageCoords(mousePos);
        imageCenter = {
            x: imageCoords.x + previousZoomLevel / zoomLevel * (imageCenter.x - imageCoords.x),
            y: imageCoords.y + previousZoomLevel / zoomLevel * (imageCenter.y - imageCoords.y)
        };
        updateZoom(zoomLevel, true);
    });

    $("#overlay").on("mousedown", (event) => {
        if (event.button != 0)
            return;
        if (event.ctrlKey) {
            isZoomingToRegion = true;
            initialZoomToRegionPos = getMousePos(canvasGL, event);
        }
        else {
            isDragging = true;
            dragStarted = true;
        }
    });

    $("#overlay").on("contextmenu", () => {
        return false;
    });

    $("#overlay").on("mouseup", (event) => {
        // Recenter on middle click
        if (event.button == 1) {
            var mousePos = getMousePos(canvasGL, event);
            var imageCoords = getImageCoords(mousePos);
            // Shift key restricts panning to a single dimension (whichever is a larger delta)
            if (event.shiftKey) {
                var deltaX = Math.abs(imageCenter.x - imageCoords.x);
                var deltaY = Math.abs(imageCenter.y - imageCoords.y);
                if (deltaX > deltaY)
                    imageCenter.x = imageCoords.x;
                else
                    imageCenter.y = imageCoords.y;
            }
            else
                imageCenter = imageCoords;
            updateBounds(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);
            var vertices = getGLCoords(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);
            updateVertices(vertices);
            refreshColorScheme();
            previousDragLocation = mousePos;
            $("#centerX").val(imageCenter.x);
            $("#centerY").val(imageCenter.y);
            updateProfilesAndCursor(mousePos);
        }
        else if (event.button != 0)
            return;

        if (isZoomingToRegion) {
            isZoomingToRegion = false;
            overlay.clearRect(0, 0, canvasSize.x, canvasSize.y);
            var mousePos = getMousePos(canvasGL, event);
            // ignore regions with zero height or width
            if (initialZoomToRegionPos.x === mousePos.x || initialZoomToRegionPos.y === mousePos.y)
                return;
            // do zoom to region update here
            var imageCoordsStart = getImageCoords(initialZoomToRegionPos);
            var imageCoordsEnd = getImageCoords(mousePos);

            var zoomLevelX = canvasSize.x / Math.abs(mousePos.x - initialZoomToRegionPos.x) * zoomLevel;
            var zoomLevelY = canvasSize.y / Math.abs(mousePos.y - initialZoomToRegionPos.y) * zoomLevel;
            imageCenter = {x: (imageCoordsStart.x + imageCoordsEnd.x) / 2.0, y: (imageCoordsStart.y + imageCoordsEnd.y) / 2.0};
            updateZoom(Math.min(zoomLevelX, zoomLevelY), false);
        }
        else {
            isDragging = false;
            dragStarted = false;
            checkAndUpdateRegion();
        }
    });

    $("#overlay").on("mouseenter", (event) => {
        isDragging = isDragging && (event.originalEvent.buttons & 1);
    });

    $("#overlay").on("mouseleave", (event) => {
        if (isDragging)
            checkAndUpdateRegion();
    });


    connection.onopen = () => {
        console.log("Connected");
    };

    // Log errors
    connection.onerror = error => {
        console.log("WebSocket Error " + error);
    };

    // Log messages from the server
    connection.onmessage = (event) => {
        var binaryPayload = null;
        var jsonPayload = null;
        if (event.data instanceof ArrayBuffer) {
            var binaryLength = new DataView(event.data.slice(0, 4)).getUint32(0, true);

            binaryPayload = new Uint8Array(event.data.slice(4, 4 + binaryLength));
            jsonPayload = String.fromCharCode.apply(null, new Uint8Array(event.data, 4 + binaryLength));
        }
        else
            jsonPayload = event.data;

        var eventData = JSON.parse(jsonPayload);
        var eventName = eventData.event;
        var message = eventData.message;

        if (eventName === "region_read" && message.success) {
            regionImageData = message;
            if (regionImageData.compression >= 4 && regionImageData.compression < 32) {
                var nanEncodingLength = new DataView(event.data.slice(4, 8)).getUint32(0, true);
                var nanEncodings = new Int32Array(event.data.slice(8, 8 + 4 * nanEncodingLength));
                var compressedDataLength = binaryLength - 4 - nanEncodingLength;
                var compressedData = new Uint8Array(event.data.slice(8 + 4 * nanEncodingLength, 8 + 4 * nanEncodingLength + compressedDataLength));
                regionImageData.fp32payload = zfpDecompressUint8WASM(compressedData, regionImageData.w, regionImageData.h, regionImageData.compression);
                binaryPayload.length = 0;

                // put NaNs back into data
                var decodedIndex = 0;
                var fillVal = false;
                for (var i = 0; i < nanEncodingLength; i++) {
                    var L = nanEncodings[i];
                    for (var j = 0; j < L; j++) {
                        regionImageData.fp32payload[j + decodedIndex] = fillVal ? NaN : regionImageData.fp32payload[j + decodedIndex];
                    }
                    fillVal = !fillVal;
                    decodedIndex += L;
                }

                updateProfilesAndCursor(cursorPos, false);
                requestAnimationFrame(function () {
                    var hist = message.hist;
                    if (!hist || !hist.bins || !hist.bins.length || !hist.N || !hist.firstBinCenter || !hist.binWidth) {
                        histogram.data.datasets[0].data.length = 0;
                        histogram.update({duration: 0});
                        return;
                    }

                    histogram.data.datasets[0].data.length = hist.N;
                    for (var i = 0; i < hist.N; i++) {
                        histogram.data.datasets[0].data[i] = {x: hist.firstBinCenter + i * hist.binWidth, y: Math.max(0.1, hist.bins[i])};
                    }
                    histogram.update({duration: 0});
                });
            }
            else
                regionImageData.fp32payload = new Float32Array(binaryPayload.buffer);

            if (extTextureFloat) {
                loadFP32Texture(regionImageData.fp32payload, regionImageData.w, regionImageData.h);
            }
            else {
                regionImageData.u8payload = encodeToUint8WASM(regionImageData.fp32payload);
                loadRGBATexture(regionImageData.u8payload, regionImageData.w, regionImageData.h);
            }

            currentRegion = {
                x: regionImageData.x,
                y: regionImageData.y,
                w: regionImageData.w * regionImageData.mip,
                h: regionImageData.h * regionImageData.mip,
                mip: regionImageData.mip,
                band: regionImageData.band,
                compression: regionImageData.compression
            };

            $("#current_view_x").html(currentRegion.x);
            $("#current_view_y").html(currentRegion.y);
            $("#current_view_w").html(currentRegion.w);
            $("#current_view_h").html(currentRegion.h);
            $("#current_view_mip").html(currentRegion.mip);

            var vertices = getGLCoords(imageCenter, imageSize, currentRegion, canvasSize, zoomLevel);
            updateVertices(vertices);
            if (message.stats) {
                bandStats = message.stats;
                updateSliders(bandStats);
            }
            else
                refreshColorScheme();
        }
        else if (eventName === "fileload" && message.success) {
            $("#band_val").val(-1);
            $("#band_val").attr({
                "max": message.numBands - 1
            });
            imageSize = {x: message.width, y: message.height};
            imageCenter.x = imageSize.x / 2;
            imageCenter.y = imageSize.y / 2;

            updateZoom(Math.min(canvasSize.x / imageSize.x, canvasSize.y / imageSize.y), false);
        }


    };

    function updateSliders(stats) {
        if (!stats || stats.minVal === undefined || stats.maxVal === undefined)
            return;

        $("#min_val").attr({
            "min": stats.minVal,
            "max": stats.maxVal,
            "step": (stats.maxVal - stats.minVal) / 1000
        });
        $("#max_val").attr({
            "min": stats.minVal,
            "max": stats.maxVal,
            "step": (stats.maxVal - stats.minVal) / 1000
        });

        if ($("#percentile_select").val() !== "custom" && stats.percentiles && stats.percentileVals && stats.percentiles.length === stats.percentileVals.length) {
            var percentilesFixed = stats.percentiles.map(v => v.toFixed(3));
            var selectedPercentile = $("#percentile_select").val();
            var inversePercentile = (100 - parseFloat(selectedPercentile)).toFixed(3);
            var indexPercentileLow = percentilesFixed.indexOf(inversePercentile);
            var indexPercentileHigh = percentilesFixed.indexOf(selectedPercentile);

            if (indexPercentileHigh >= 0 && indexPercentileHigh >= 0) {
                minVal = stats.percentileVals[indexPercentileLow];
                maxVal = stats.percentileVals[indexPercentileHigh];
                $("#min_val").val(minVal);
                $("#min_val_label").text(minVal);
                $("#max_val").val(maxVal);
                $("#max_val_label").text(maxVal);
                histogram.annotation.options.annotations[1].value = minVal;
                histogram.annotation.options.annotations[2].value = maxVal;
                histogram.update({duration: 0});
            }
        }
        refreshColorScheme();
    }

    function refreshColorScheme() {
        if (!regionImageData)
            return;
        gl.uniform1f(shaderProgram.MinValUniform, minVal);
        gl.uniform1f(shaderProgram.MaxValUniform, maxVal);
        gl.uniform1i(shaderProgram.CmapIndex, $("#cmap_select").val());
        gl.uniform2f(shaderProgram.ViewportSizeUniform, gl.viewportWidth, gl.viewportHeight);
        requestAnimationFrame(drawScene);
        //drawScene();
    }

    function encodeToUint8WASM(f) {
        encodeFloats = Module.cwrap(
            "encodeFloats", "number", ["number", "number", "number"]
        );
        var nDataBytes = f.length * f.BYTES_PER_ELEMENT;
        var dataPtr = Module._malloc(nDataBytes);
        var dataPtrUint = Module._malloc(nDataBytes);
        var dataHeap = new Uint8Array(Module.HEAPU8.buffer, dataPtr, nDataBytes);
        dataHeap.set(new Uint8Array(f.slice(0, f.length).buffer));
        var dataHeapUint = new Uint8Array(Module.HEAPU8.buffer, dataPtrUint, nDataBytes);
        dataHeapUint.set(new Uint8Array(dataPtrUint.buffer));

        // Call function and get result
        encodeFloats(dataHeap.byteOffset, dataHeapUint.byteOffset, f.length);
        var resultUint = new Uint8Array(dataHeapUint.buffer, dataHeapUint.byteOffset, f.length * 4);
        var outUint = resultUint.slice();
        // Free memory
        Module._free(dataHeap.byteOffset);
        Module._free(dataHeapUint.byteOffset);

        return outUint;
        // END WASM

    }

    function zfpDecompressUint8WASM(u8, nx, ny, precision) {
        var zfpDecompress = Module.cwrap(
            "zfpDecompress", "number", ["number", "number", "number", "number", "number", "number"]
        );

        var newNumDataBytes = nx * ny * 4;
        if (!dataPtr || newNumDataBytes > nDataBytes) {
            if (dataHeap)
                Module._free(dataHeap.byteOffset);
            nDataBytes = newNumDataBytes;
            dataPtr = Module._malloc(nDataBytes);
            dataHeap = new Uint8Array(Module.HEAPU8.buffer, dataPtr, nDataBytes);
            console.log(`Allocating new uncompressed buffer (${nDataBytes / 1000} KB)`);
            resultFloat = new Float32Array(dataHeap.buffer, dataHeap.byteOffset, nx * ny);
        }

        var newNumDataBytesCompressed = u8.length;
        if (!dataPtrUint || newNumDataBytesCompressed > nDataBytesCompressed) {
            if (dataHeapUint)
                Module._free(dataHeapUint.byteOffset);
            nDataBytesCompressed = newNumDataBytesCompressed;
            dataPtrUint = Module._malloc(nDataBytesCompressed);
            dataHeapUint = new Uint8Array(Module.HEAPU8.buffer, dataPtrUint, nDataBytesCompressed);
            console.log(`Allocating new compressed buffer (${nDataBytesCompressed / 1000} KB)`);
        }

        dataHeapUint.set(new Uint8Array(u8.buffer));
        // Call function and get result
        zfpDecompress(parseInt(precision), dataHeap.byteOffset, nx, ny, dataHeapUint.byteOffset, u8.length);

        // Free memory
        return resultFloat.slice();
        // END WASM

    }

    function encodeToUint8(f) {
        var u8 = new Uint8Array(4 * f.length);
        var dv = new DataView(f.buffer);

        for (var i = 0, offset = 0; i < f.length; i++, offset += 4) {
            var xi = dv.getUint32(offset, true);
            var v = -1;
            v = (xi >> 31 & 1) | ((xi & 0x7fffff) << 1);
            u8[offset] = v / 0x10000;
            u8[offset + 1] = (v % 0x10000) / 0x100;
            u8[offset + 2] = v % 0x100;
            u8[offset + 3] = xi >> 23 & 0xff;
        }
        return u8;
    }

    function hexToRGB(hex) {
        hex = hex.toUpperCase();
        var h = "0123456789ABCDEF";
        var r = h.indexOf(hex[1]) * 16 + h.indexOf(hex[2]);
        var g = h.indexOf(hex[3]) * 16 + h.indexOf(hex[4]);
        var b = h.indexOf(hex[5]) * 16 + h.indexOf(hex[6]);
        return {r, g, b};
    }

    function getMousePos(canvas, evt) {
        var rect = canvas.getBoundingClientRect();
        return {
            x: evt.clientX - rect.left,
            y: evt.clientY - rect.top
        };
    }

    $("#region").click(checkAndUpdateRegion);

    $("#button_zoom_fit").click(function () {
        imageCenter.x = imageSize.x / 2;
        imageCenter.y = imageSize.y / 2;
        updateZoom(Math.min(canvasSize.x / imageSize.x, canvasSize.y / imageSize.y), false);
    });

    $("#button_zoom_fit_v").click(function () {
        imageCenter.y = imageSize.y / 2;
        updateZoom(canvasSize.y / imageSize.y, false);
    });

    $("#button_zoom_fit_h").click(function () {
        imageCenter.x = imageSize.x / 2;
        updateZoom(canvasSize.x / imageSize.x, false);
    });

    $("#button_zoom_100").click(function () {
        updateZoom(1.0, false);
    });

    $("#button_zoom_50").click(function () {
        updateZoom(0.5, false);
    });

    $("#button_zoom_33").click(function () {
        updateZoom(0.3333333, false);
    });

    $("#button_zoom_25").click(function () {
        updateZoom(0.25, false);
    });

    $("#button_zoom_200").click(function () {
        updateZoom(2.0, false);
    });

    $("#button_load").click(function () {
        if (connection) {
            var payload = {
                event: "fileload",
                message: {
                    filename: $("#fileload_data").val()
                }
            };
            connection.send(JSON.stringify(payload));
        }
        return false;
    });

});