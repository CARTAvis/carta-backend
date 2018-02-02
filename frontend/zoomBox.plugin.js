Chart.plugins.register({
    beforeInit: function (chart) {
        // Merge default options with options specified in the chart.options.zoomBox object
        var defaultOptions = {
            doubleClickDelay: 300,
            deltaLimit: 20,
            mode: 'xy',
            rectangleSelect: true,
            rescale: true,
            rescaleMarginPercent: 5,
            fillStyle: "#e0d7d955",
            lineWidth: 1,
            strokeStyle: "grey",
            showOverlay: true,
            strokeStyleOverlay: "darkgrey",
            lineWidthOverlay: 3
        };
        chart.zoomBox = {
            options: Chart.helpers.configMerge(defaultOptions, chart.options.zoomBox)
        };

        // Sanity checks for zoom modes. Can only do rectangle select if both zoom directions are enabled
        if (chart.zoomBox.options.mode !== 'xy')
            chart.zoomBox.options.rectangleSelect = false;

    },
    afterEvent: function (chart, event) {
        event.native.preventDefault();
        var options = chart.zoomBox.options;

        if (event.type === "mousedown") {
            chart.zoomBox.pixelInitial = {x: clamp(event.x, chart.chartArea.left, chart.chartArea.right), y: clamp(event.y, chart.chartArea.top, chart.chartArea.bottom)};
            chart.zoomBox.scaleInitial = getScaledValues(chart, chart.zoomBox.pixelInitial);
            chart.zoomBox.dragging = true;
            chart.zoomBox.pixelFinal = null;
            chart.zoomBox.scaleFinal = null;
        }
        else if (event.type === "mouseup") {
            // intercept double clicks if they lie within the doubleClickDelay window. Reset zoom on double click
            var tClick = Date.now();
            if (chart.zoomBox.prevClick > 0 && (tClick - chart.zoomBox.prevClick) < options.doubleClickDelay) {
                chart.zoomBox.prevClick = -1;

                if (chart.zoomBox.originalZoom) {
                    chart.options.scales.xAxes[0].ticks.min = chart.zoomBox.originalZoom.xMin;
                    chart.options.scales.xAxes[0].ticks.max = chart.zoomBox.originalZoom.xMax;
                    chart.options.scales.yAxes[0].ticks.min = chart.zoomBox.originalZoom.yMin;
                    chart.options.scales.yAxes[0].ticks.max = chart.zoomBox.originalZoom.yMax;
                    chart.update({duration: 0});
                }
                chart.zoomBox.dragging = false;
                chart.zoomBox.pixelCurrent = null;
                chart.zoomBox.scaleCurrent = null;
                chart.zoomBox.pixelFinal = null;
                chart.zoomBox.scaleFinal = null;
                chart.zoomBox.pixelInitial = null;
                chart.zoomBox.scaleInitial = null;
            }
            // Not a double click, continue with normal handling
            else {
                chart.zoomBox.prevClick = tClick;
                chart.zoomBox.pixelFinal = {x: clamp(event.x, chart.chartArea.left, chart.chartArea.right), y: clamp(event.y, chart.chartArea.top, chart.chartArea.bottom)};
                chart.zoomBox.scaleFinal = getScaledValues(chart, chart.zoomBox.pixelFinal);
                chart.zoomBox.dragging = false;

                // Prevents zooming in if no drag has occured (i.e. on the first click of a double click)
                if (chart.zoomBox.pixelCurrent) {
                    chart.zoomBox.pixelCurrent = null;
                    chart.zoomBox.scaleCurrent = null;

                    // save zoom settings before zooming in
                    if (!chart.zoomBox.originalZoom) {
                        chart.zoomBox.originalZoom = {
                            xMin: chart.options.scales.xAxes[0].ticks.min,
                            xMax: chart.options.scales.xAxes[0].ticks.max,
                            yMin: chart.options.scales.yAxes[0].ticks.min,
                            yMax: chart.options.scales.yAxes[0].ticks.max
                        };
                    }

                    if (chart.zoomBox.currentZoomMode == 'xy') {
                        chart.options.scales.xAxes[0].ticks.min = Math.min(chart.zoomBox.scaleInitial.x, chart.zoomBox.scaleFinal.x);
                        chart.options.scales.xAxes[0].ticks.max = Math.max(chart.zoomBox.scaleInitial.x, chart.zoomBox.scaleFinal.x);
                        chart.options.scales.yAxes[0].ticks.min = Math.min(chart.zoomBox.scaleInitial.y, chart.zoomBox.scaleFinal.y);
                        chart.options.scales.yAxes[0].ticks.max = Math.max(chart.zoomBox.scaleInitial.y, chart.zoomBox.scaleFinal.y);
                        chart.update({duration: 0});
                    }
                    else if (chart.zoomBox.currentZoomMode == 'x') {
                        chart.options.scales.xAxes[0].ticks.min = Math.min(chart.zoomBox.scaleInitial.x, chart.zoomBox.scaleFinal.x);
                        chart.options.scales.xAxes[0].ticks.max = Math.max(chart.zoomBox.scaleInitial.x, chart.zoomBox.scaleFinal.x);
                        // Rescale Y axis to fit new data range
                        if (options.rescale && chart.data.datasets[0].data && chart.data.datasets[0].data.length) {
                            var datasetMinVal = chart.data.datasets[0].data[0].y;
                            var datasetMaxVal = chart.data.datasets[0].data[0].y;
                            var foundPoint = false;
                            for (var point of chart.data.datasets[0].data) {
                                if (isNaN(point.y) || point.x < chart.options.scales.xAxes[0].ticks.min || point.x > chart.options.scales.xAxes[0].ticks.max)
                                    continue;
                                datasetMinVal = Math.min(datasetMinVal, point.y);
                                datasetMaxVal = Math.max(datasetMaxVal, point.y);
                                foundPoint = true;
                            }

                            if (foundPoint) {
                                var height = datasetMaxVal - datasetMinVal;
                                chart.options.scales.yAxes[0].ticks.min = datasetMinVal - options.rescaleMarginPercent / 100.0 * height;
                                chart.options.scales.yAxes[0].ticks.max = datasetMaxVal + options.rescaleMarginPercent / 100.0 * height;
                            }
                        }


                        chart.update({duration: 0});
                    }
                    else if (chart.zoomBox.currentZoomMode == 'y') {
                        chart.options.scales.yAxes[0].ticks.min = Math.min(chart.zoomBox.scaleInitial.y, chart.zoomBox.scaleFinal.y);
                        chart.options.scales.yAxes[0].ticks.max = Math.max(chart.zoomBox.scaleInitial.y, chart.zoomBox.scaleFinal.y);
                        chart.update({duration: 0});
                    }
                }
            }
        }
        else if (event.type === "mousemove" && chart.zoomBox && chart.zoomBox.dragging) {
            // if the left mouse button is no longer down, cancel the zoom event.
            // This occurs if the "mouseup" event occurs off the chart element and isn't captured
            if (event.native.buttons < 1 || event.native.button !== 0) {
                chart.zoomBox.dragging = false;
                chart.zoomBox.pixelCurrent = null;
                chart.zoomBox.scaleCurrent = null;
                chart.zoomBox.pixelFinal = null;
                chart.zoomBox.scaleFinal = null;
                chart.zoomBox.pixelInitial = null;
                chart.zoomBox.scaleInitial = null;
            }
            else {
                chart.zoomBox.pixelCurrent = {x: clamp(event.x, chart.chartArea.left, chart.chartArea.right), y: clamp(event.y, chart.chartArea.top, chart.chartArea.bottom)};
                chart.zoomBox.scaleCurrent = getScaledValues(chart, chart.zoomBox.pixelCurrent);
            }
        }
    },
    beforeDraw: function (chart, easing) {
        var ctx = chart.chart.ctx;
        var options = chart.zoomBox.options;
        if (chart.zoomBox && chart.zoomBox.dragging && chart.zoomBox.pixelInitial && chart.zoomBox.pixelCurrent) {

            var dX = chart.zoomBox.pixelCurrent.x - chart.zoomBox.pixelInitial.x;
            var dY = chart.zoomBox.pixelCurrent.y - chart.zoomBox.pixelInitial.y;

            // Rectangle mode
            if (options.rectangleSelect && Math.abs(dX) > options.deltaLimit && Math.abs(dY) > options.deltaLimit) {
                chart.zoomBox.currentZoomMode = 'xy';
                ctx.save();
                ctx.fillStyle = options.fillStyle;
                ctx.fillRect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                ctx.lineWidth = options.lineWidth;
                ctx.strokeStyle = options.strokeStyle;
                ctx.rect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                ctx.stroke();
                ctx.rect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                if (options.showOverlay) {
                    ctx.lineWidth = options.lineWidthOverlay;
                    ctx.strokeStyle = options.strokeStyleOverlay;
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y + (dY > 0 ? 1 : -1) * options.deltaLimit / 2);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x + (dX > 0 ? 1 : -1) * options.deltaLimit / 2, chart.zoomBox.pixelInitial.y);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelCurrent.y - (dY > 0 ? 1 : -1) * options.deltaLimit / 2);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelCurrent.y);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x + (dX > 0 ? 1 : -1) * options.deltaLimit / 2, chart.zoomBox.pixelCurrent.y);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelInitial.y + (dY > 0 ? 1 : -1) * options.deltaLimit / 2);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelInitial.y);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x - (dX > 0 ? 1 : -1) * options.deltaLimit / 2, chart.zoomBox.pixelInitial.y);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelCurrent.y - (dY > 0 ? 1 : -1) * options.deltaLimit / 2);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelCurrent.y);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x - (dX > 0 ? 1 : -1) * options.deltaLimit / 2, chart.zoomBox.pixelCurrent.y);
                    ctx.stroke();
                }
                ctx.restore();
            }
            // H-Zoom only (vertical lines)
            else if ((options.mode === 'xy' && Math.abs(dX) > Math.abs(dY)) || options.mode === 'x') {
                chart.zoomBox.currentZoomMode = 'x';
                ctx.save();
                ctx.fillStyle = options.fillStyle;
                ctx.fillRect(chart.zoomBox.pixelInitial.x, chart.chartArea.top, dX, chart.chartArea.bottom - chart.chartArea.top);
                ctx.lineWidth = options.lineWidth;
                ctx.strokeStyle = options.strokeStyle;
                ctx.rect(chart.zoomBox.pixelInitial.x, chart.chartArea.top, dX, chart.chartArea.bottom - chart.chartArea.top);
                ctx.stroke();
                if (options.showOverlay) {
                    var yMin = clamp(chart.zoomBox.pixelInitial.y - options.deltaLimit, chart.chartArea.top, chart.chartArea.bottom);
                    var yMax = clamp(chart.zoomBox.pixelInitial.y + options.deltaLimit, chart.chartArea.top, chart.chartArea.bottom)
                    ctx.lineWidth = options.lineWidthOverlay;
                    ctx.strokeStyle = options.strokeStyleOverlay;
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelInitial.x, yMin);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x, yMax);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelCurrent.x, yMin);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x, yMax);
                    ctx.stroke();
                }
                ctx.restore();
            }
            // V-Zoom only (horizontal lines)
            else if (options.mode === 'xy' || options.mode === 'y') {
                chart.zoomBox.currentZoomMode = 'y';
                ctx.save();
                ctx.fillStyle = options.fillStyle;
                ctx.fillRect(chart.chartArea.left, chart.zoomBox.pixelInitial.y, chart.chartArea.right - chart.chartArea.left, dY);
                ctx.lineWidth = options.lineWidth;
                ctx.strokeStyle = options.strokeStyle;
                ctx.rect(chart.chartArea.left, chart.zoomBox.pixelInitial.y, chart.chartArea.right - chart.chartArea.left, dY);
                ctx.stroke();
                if (options.showOverlay) {
                    var xMin = clamp(chart.zoomBox.pixelInitial.x - options.deltaLimit, chart.chartArea.left, chart.chartArea.right);
                    var xMax = clamp(chart.zoomBox.pixelInitial.x + options.deltaLimit, chart.chartArea.left, chart.chartArea.right);
                    ctx.lineWidth = options.lineWidthOverlay;
                    ctx.strokeStyle = options.strokeStyleOverlay;
                    ctx.beginPath();
                    ctx.moveTo(xMin, chart.zoomBox.pixelInitial.y);
                    ctx.lineTo(xMax, chart.zoomBox.pixelInitial.y);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(xMin, chart.zoomBox.pixelCurrent.y);
                    ctx.lineTo(xMax, chart.zoomBox.pixelCurrent.y);
                    ctx.stroke();
                }
                ctx.restore();
            }
        }
    }
});

function getScaledValues(chart, pixel) {
    var x = 0;
    var y = 0;

    for (var scaleName in chart.scales) {
        var scale = chart.scales[scaleName];
        if (scale.isHorizontal()) {
            x = scale.getValueForPixel(pixel.x);
        }
        else {
            y = scale.getValueForPixel(pixel.y);
        }
    }
    return {x, y};
}

function clamp(num, min, max) {
    return num <= min ? min : num >= max ? max : num;
}
