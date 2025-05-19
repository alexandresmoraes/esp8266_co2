/*
 * Plotly.js Basic - Lightweight implementation for ESP8266
 * This version is optimized for rendering simple line and bar charts on NodeMCU
 */
(function() {
  // Main Plotly object
  var Plotly = {
    newPlot: function(containerId, data, layout, config) {
      var container = typeof containerId === 'string' ? document.getElementById(containerId) : containerId;
      if (!container) {
        console.error('Container not found:', containerId);
        return null;
      }
      
      // Clear container
      container.innerHTML = '';
      
      // Default layout and config
      layout = layout || {};
      config = config || {};
      
      // Create a canvas for rendering
      var canvas = document.createElement('canvas');
      canvas.style.width = '100%';
      canvas.style.height = '100%';
      container.appendChild(canvas);
      
      // Set canvas dimensions based on container
      var rect = container.getBoundingClientRect();
      canvas.width = rect.width;
      canvas.height = rect.height;
      
      // Get canvas context
      var ctx = canvas.getContext('2d');
      
      // Store the chart data
      var chart = {
        data: data || [],
        layout: layout,
        config: config,
        container: container,
        canvas: canvas,
        ctx: ctx
      };
      
      // Store chart in container for reference
      container._plotly_chart = chart;
      
      // Draw the chart
      drawChart(chart);
      
      // Add resize handler if responsive
      if (config.responsive !== false) {
        var resizeHandler = function() {
          if (container.offsetParent !== null) { // Only resize visible elements
            var rect = container.getBoundingClientRect();
            canvas.width = rect.width;
            canvas.height = rect.height;
            drawChart(chart);
          }
        };
        
        window.addEventListener('resize', resizeHandler);
        chart._resizeHandler = resizeHandler;
      }
      
      return chart;
    },
    
    update: function(containerId, dataUpdate, layoutUpdate) {
      var container = typeof containerId === 'string' ? document.getElementById(containerId) : containerId;
      if (!container || !container._plotly_chart) return false;
      
      var chart = container._plotly_chart;
      
      // Update data
      if (dataUpdate) {
        if (dataUpdate.x && Array.isArray(dataUpdate.x)) {
          for (var i = 0; i < Math.min(dataUpdate.x.length, chart.data.length); i++) {
            if (chart.data[i]) chart.data[i].x = dataUpdate.x[i];
          }
        }
        
        if (dataUpdate.y && Array.isArray(dataUpdate.y)) {
          for (var i = 0; i < Math.min(dataUpdate.y.length, chart.data.length); i++) {
            if (chart.data[i]) chart.data[i].y = dataUpdate.y[i];
          }
        }
      }
      
      // Update layout
      if (layoutUpdate) {
        for (var key in layoutUpdate) {
          if (layoutUpdate.hasOwnProperty(key)) {
            chart.layout[key] = layoutUpdate[key];
          }
        }
      }
      
      // Redraw the chart
      drawChart(chart);
      
      return true;
    },
    
    react: function(containerId, data, layout) {
      var container = typeof containerId === 'string' ? document.getElementById(containerId) : containerId;
      if (!container || !container._plotly_chart) return false;
      
      var chart = container._plotly_chart;
      chart.data = Array.isArray(data) ? data : [];
      
      if (layout) {
        chart.layout = layout;
      }
      
      drawChart(chart);
      return true;
    },
    
    purge: function(containerId) {
      var container = typeof containerId === 'string' ? document.getElementById(containerId) : containerId;
      if (!container) return false;
      
      if (container._plotly_chart && container._plotly_chart._resizeHandler) {
        window.removeEventListener('resize', container._plotly_chart._resizeHandler);
      }
      
      container.innerHTML = '';
      delete container._plotly_chart;
      
      return true;
    }
  };
  
  // Helper function to draw charts
  function drawChart(chart) {
    if (!chart || !chart.ctx) return;
    
    var ctx = chart.ctx;
    var canvas = chart.canvas;
    var data = Array.isArray(chart.data) ? chart.data : [];
    var layout = chart.layout || {};
    
    // Clear canvas
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    
    // Calculate margin
    var margin = layout.margin || { l: 50, r: 30, t: 30, b: 40 };
    var plotWidth = canvas.width - margin.l - margin.r;
    var plotHeight = canvas.height - margin.t - margin.b;
    
    // Draw title
    if (layout.title) {
      var title = typeof layout.title === 'object' ? layout.title.text : layout.title;
      ctx.fillStyle = '#444';
      ctx.font = 'bold 14px Arial';
      ctx.textAlign = 'center';
      ctx.fillText(title, canvas.width / 2, margin.t / 2);
    }
    
    // Draw axes
    ctx.strokeStyle = '#ccc';
    ctx.lineWidth = 1;
    
    // Y-axis
    ctx.beginPath();
    ctx.moveTo(margin.l, margin.t);
    ctx.lineTo(margin.l, canvas.height - margin.b);
    ctx.stroke();
    
    // X-axis
    ctx.beginPath();
    ctx.moveTo(margin.l, canvas.height - margin.b);
    ctx.lineTo(canvas.width - margin.r, canvas.height - margin.b);
    ctx.stroke();
    
    // Skip if no data
    if (!data || data.length === 0) return;
    
    // Find min/max values
    var allX = [];
    var primaryY = [];
    var secondaryY = [];
    
    data.forEach(function(trace) {
      if (!trace || !trace.x || !trace.y || !Array.isArray(trace.x) || !Array.isArray(trace.y)) return;
      
      // Collect X values
      if (typeof trace.x[0] === 'number') {
        allX = allX.concat(trace.x);
      } else {
        // For categories/strings, just use indices
        for (var i = 0; i < trace.x.length; i++) {
          if (allX.indexOf(trace.x[i]) === -1) {
            allX.push(trace.x[i]);
          }
        }
      }
      
      // Collect Y values
      var yArray = trace.y.filter(function(val) {
        return val !== null && val !== undefined && !isNaN(val);
      });
      
      if (trace.yaxis === 'y2') {
        secondaryY = secondaryY.concat(yArray);
      } else {
        primaryY = primaryY.concat(yArray);
      }
    });
    
    // Determine ranges
    var xIsNumeric = typeof allX[0] === 'number';
    
    // Fix: Handle empty arrays better and avoid NaN issues
    if (allX.length === 0) {
      allX = [0];
    }
    if (primaryY.length === 0) {
      primaryY = [0];
    }
    
    var xMin = xIsNumeric ? Math.min.apply(null, allX) : 0;
    var xMax = xIsNumeric ? Math.max.apply(null, allX) : allX.length - 1;
    
    // Fix: Better range handling to avoid NaN issues
    if (xMin === xMax) {
      xMin = xMin - 1;
      xMax = xMax + 1;
    }
    
    var yMin = layout.yaxis && layout.yaxis.range ? layout.yaxis.range[0] : 
               Math.min(0, Math.min.apply(null, primaryY) * 0.9);
    var yMax = layout.yaxis && layout.yaxis.range ? layout.yaxis.range[1] : 
               Math.max.apply(null, primaryY) * 1.1;
               
    // Fix: Better range handling to avoid NaN issues
    if (yMin === yMax) {
      yMin = yMin - 1;
      yMax = yMax + 1;
    }
    
    // Fix for secondary Y axis when no data is present
    if (secondaryY.length === 0) {
      secondaryY = [0];
    }
    
    var y2Min = layout.yaxis2 && layout.yaxis2.range ? layout.yaxis2.range[0] : 
                Math.min(0, Math.min.apply(null, secondaryY) * 0.9);
    var y2Max = layout.yaxis2 && layout.yaxis2.range ? layout.yaxis2.range[1] : 
                Math.max.apply(null, secondaryY) * 1.1 || 100;
                
    // Fix: Better range handling to avoid NaN issues
    if (y2Min === y2Max) {
      y2Min = y2Min - 1;
      y2Max = y2Max + 1;
    }
    
    // Fix: Ensure we have valid number ranges
    xMin = isFinite(xMin) ? xMin : 0;
    xMax = isFinite(xMax) ? xMax : 10;
    yMin = isFinite(yMin) ? yMin : 0;
    yMax = isFinite(yMax) ? yMax : 100;
    y2Min = isFinite(y2Min) ? y2Min : 0;
    y2Max = isFinite(y2Max) ? y2Max : 100;
    
    // Draw axis labels
    if (layout.xaxis && layout.xaxis.title) {
      var xTitle = typeof layout.xaxis.title === 'object' ? layout.xaxis.title.text : layout.xaxis.title;
      ctx.fillStyle = '#666';
      ctx.font = '12px Arial';
      ctx.textAlign = 'center';
      ctx.fillText(xTitle, margin.l + plotWidth / 2, canvas.height - 10);
    }
    
    if (layout.yaxis && layout.yaxis.title) {
      var yTitle = typeof layout.yaxis.title === 'object' ? layout.yaxis.title.text : layout.yaxis.title;
      ctx.save();
      ctx.translate(15, margin.t + plotHeight / 2);
      ctx.rotate(-Math.PI / 2);
      ctx.fillStyle = '#666';
      ctx.font = '12px Arial';
      ctx.textAlign = 'center';
      ctx.fillText(yTitle, 0, 0);
      ctx.restore();
    }
    
    if (layout.yaxis2 && layout.yaxis2.title) {
      var y2Title = typeof layout.yaxis2.title === 'object' ? layout.yaxis2.title.text : layout.yaxis2.title;
      ctx.save();
      ctx.translate(canvas.width - 15, margin.t + plotHeight / 2);
      ctx.rotate(-Math.PI / 2);
      ctx.fillStyle = '#666';
      ctx.font = '12px Arial';
      ctx.textAlign = 'center';
      ctx.fillText(y2Title, 0, 0);
      ctx.restore();
    }
    
    // Draw Y-axis ticks
    var yTicks = 5;
    for (var i = 0; i <= yTicks; i++) {
      var y = yMin + (i / yTicks) * (yMax - yMin);
      var yPixel = canvas.height - margin.b - (plotHeight * (y - yMin) / (yMax - yMin));
      
      // Grid line
      ctx.strokeStyle = '#eee';
      ctx.beginPath();
      ctx.moveTo(margin.l, yPixel);
      ctx.lineTo(canvas.width - margin.r, yPixel);
      ctx.stroke();
      
      // Tick and label
      ctx.strokeStyle = '#666';
      ctx.beginPath();
      ctx.moveTo(margin.l - 5, yPixel);
      ctx.lineTo(margin.l, yPixel);
      ctx.stroke();
      
      ctx.fillStyle = '#666';
      ctx.font = '10px Arial';
      ctx.textAlign = 'right';
      ctx.fillText(Math.round(y * 100) / 100, margin.l - 8, yPixel + 3);
    }
    
    // Draw Y2-axis ticks if needed
    if (secondaryY.length > 0) {
      var y2Ticks = 5;
      for (var i = 0; i <= y2Ticks; i++) {
        var y = y2Min + (i / y2Ticks) * (y2Max - y2Min);
        var yPixel = canvas.height - margin.b - (plotHeight * (y - y2Min) / (y2Max - y2Min));
        
        // Tick and label
        ctx.strokeStyle = '#666';
        ctx.beginPath();
        ctx.moveTo(canvas.width - margin.r, yPixel);
        ctx.lineTo(canvas.width - margin.r + 5, yPixel);
        ctx.stroke();
        
        ctx.fillStyle = '#666';
        ctx.font = '10px Arial';
        ctx.textAlign = 'left';
        ctx.fillText(Math.round(y * 100) / 100, canvas.width - margin.r + 8, yPixel + 3);
      }
    }
    
    // Draw X-axis ticks
    var xTicks = Math.min(10, Math.max(2, allX.length));
    for (var i = 0; i < xTicks; i++) {
      var x, xIndex;
      
      if (xIsNumeric) {
        x = xMin + (i / (xTicks - 1)) * (xMax - xMin);
        xIndex = i;
      } else {
        xIndex = Math.floor(i * (allX.length - 1) / (xTicks - 1));
        x = allX[xIndex];
      }
      
      // Fix: Prevent division by zero
      var xPixel = margin.l;
      if (allX.length > 1) {
        xPixel = margin.l + (xIndex / (allX.length - 1)) * plotWidth;
      }
      
      // Grid line
      ctx.strokeStyle = '#eee';
      ctx.beginPath();
      ctx.moveTo(xPixel, margin.t);
      ctx.lineTo(xPixel, canvas.height - margin.b);
      ctx.stroke();
      
      // Tick and label
      ctx.strokeStyle = '#666';
      ctx.beginPath();
      ctx.moveTo(xPixel, canvas.height - margin.b);
      ctx.lineTo(xPixel, canvas.height - margin.b + 5);
      ctx.stroke();
      
      ctx.fillStyle = '#666';
      ctx.font = '10px Arial';
      ctx.textAlign = 'center';
      ctx.fillText(x, xPixel, canvas.height - margin.b + 15);
    }
    
    // Draw traces
    var legendY = margin.t;
    var legendDrawn = false;
    
    data.forEach(function(trace, traceIndex) {
      if (!trace || !trace.x || !trace.y || !Array.isArray(trace.x) || !Array.isArray(trace.y) || trace.x.length === 0 || trace.y.length === 0) return;
      
      var type = trace.type || 'scatter';
      var name = trace.name || 'Trace ' + (traceIndex + 1);
      var useSecondaryAxis = trace.yaxis === 'y2';
      
      // Choose color
      var colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b', '#e377c2', '#7f7f7f', '#bcbd22', '#17becf'];
      var color = trace.line && trace.line.color ? trace.line.color : 
                 trace.marker && trace.marker.color ? trace.marker.color :
                 colors[traceIndex % colors.length];
      
      // Map data points to pixels
      var points = [];
      for (var i = 0; i < trace.x.length; i++) {
        if (i >= trace.y.length || trace.y[i] === null || trace.y[i] === undefined || isNaN(trace.y[i])) continue;
        
        var x = xIsNumeric ? trace.x[i] : allX.indexOf(trace.x[i]);
        var y = trace.y[i];
        
        // Fix: Handle index not found
        if (x === -1 && !xIsNumeric) continue;
        
        // Fix: Better safety checks for x and y values
        if (x === undefined || y === undefined || isNaN(x) || isNaN(y)) continue;
        
        var xRange = xMax - xMin;
        var xPixel = margin.l + (xRange > 0 ? ((x - xMin) / xRange) * plotWidth : plotWidth / 2);
        
        var yPixel;
        if (useSecondaryAxis) {
          var y2Range = y2Max - y2Min;
          yPixel = canvas.height - margin.b - (y2Range > 0 ? ((y - y2Min) / y2Range) * plotHeight : plotHeight / 2);
        } else {
          var yRange = yMax - yMin;
          yPixel = canvas.height - margin.b - (yRange > 0 ? ((y - yMin) / yRange) * plotHeight : plotHeight / 2);
        }
        
        // Fix: Ensure point is within plot area
        xPixel = Math.max(margin.l, Math.min(canvas.width - margin.r, xPixel));
        yPixel = Math.max(margin.t, Math.min(canvas.height - margin.b, yPixel));
        
        points.push({ x: xPixel, y: yPixel });
      }
      
      // Skip if no valid points
      if (points.length === 0) return;
      
      // Draw based on type
      if (type === 'scatter' || type === 'line') {
        // Line
        if (trace.mode === undefined || trace.mode.includes('line') || trace.mode.includes('lines')) {
          ctx.strokeStyle = color;
          ctx.lineWidth = trace.line && trace.line.width ? trace.line.width : 2;
          
          ctx.beginPath();
          ctx.moveTo(points[0].x, points[0].y);
          
          if (trace.line && trace.line.shape === 'spline') {
            // Smooth curve
            for (var i = 0; i < points.length - 1; i++) {
              var xc = (points[i].x + points[i+1].x) / 2;
              var yc = (points[i].y + points[i+1].y) / 2;
              ctx.quadraticCurveTo(points[i].x, points[i].y, xc, yc);
            }
            // Last point
            if (points.length > 1) {
              ctx.quadraticCurveTo(points[points.length-2].x, points[points.length-2].y, 
                                 points[points.length-1].x, points[points.length-1].y);
            }
          } else {
            // Straight lines
            for (var i = 1; i < points.length; i++) {
              ctx.lineTo(points[i].x, points[i].y);
            }
          }
          
          ctx.stroke();
        }
        
        // Fill area
        if (trace.fill === 'tozeroy') {
          ctx.fillStyle = color.replace('rgb', 'rgba').replace(')', ', 0.1)');
          
          ctx.beginPath();
          ctx.moveTo(points[0].x, canvas.height - margin.b);
          ctx.lineTo(points[0].x, points[0].y);
          
          if (trace.line && trace.line.shape === 'spline') {
            // Smooth curve
            for (var i = 0; i < points.length - 1; i++) {
              var xc = (points[i].x + points[i+1].x) / 2;
              var yc = (points[i].y + points[i+1].y) / 2;
              ctx.quadraticCurveTo(points[i].x, points[i].y, xc, yc);
            }
            // Last point
            if (points.length > 1) {
              ctx.quadraticCurveTo(points[points.length-2].x, points[points.length-2].y, 
                                 points[points.length-1].x, points[points.length-1].y);
            }
          } else {
            // Straight lines
            for (var i = 1; i < points.length; i++) {
              ctx.lineTo(points[i].x, points[i].y);
            }
          }
          
          ctx.lineTo(points[points.length-1].x, canvas.height - margin.b);
          ctx.closePath();
          ctx.fill();
        }
        
        // Markers
        if (trace.mode === undefined || trace.mode.includes('marker') || trace.mode.includes('markers')) {
          ctx.fillStyle = color;
          var markerSize = trace.marker && trace.marker.size ? trace.marker.size : 6;
          
          for (var i = 0; i < points.length; i++) {
            ctx.beginPath();
            ctx.arc(points[i].x, points[i].y, markerSize / 2, 0, 2 * Math.PI);
            ctx.fill();
          }
        }
      } else if (type === 'bar') {
        // Bar chart
        var barTraces = data.filter(function(t) { return t.type === 'bar'; }).length;
        var barIndex = data.filter(function(t, i) { return t.type === 'bar' && i < traceIndex; }).length;
        
        // Fix: Handle division by zero and prevent negative bar width
        var barWidth = Math.max(1, plotWidth / Math.max(1, allX.length) * 0.8 / Math.max(1, barTraces));
        var barOffset = -0.4 * plotWidth / Math.max(1, allX.length) + barIndex * barWidth;
        
        ctx.fillStyle = color;
        
        for (var i = 0; i < points.length; i++) {
          var barHeight = canvas.height - margin.b - points[i].y;
          var barX = points[i].x + barOffset;
          
          // Fix: Make sure bar dimensions are positive
          barWidth = Math.max(1, barWidth);
          barHeight = Math.max(0, barHeight);
          
          // Fix: Ensure bar is within plot area
          barX = Math.max(margin.l, Math.min(canvas.width - margin.r - barWidth, barX));
          
          ctx.fillRect(barX, points[i].y, barWidth, barHeight);
          
          // Bar border
          ctx.strokeStyle = 'rgba(0,0,0,0.3)';
          ctx.strokeRect(barX, points[i].y, barWidth, barHeight);
        }
      }
      
      // Add to legend
      if (layout.showlegend !== false && trace.name) {
        // Skip if already drawn for this trace
        if (!legendDrawn) {
          legendDrawn = true;
          
          // Calculate legend position
          var legendX = layout.legend && layout.legend.x ? 
                      margin.l + plotWidth * layout.legend.x : 
                      canvas.width - margin.r + 5;
          
          legendY = layout.legend && layout.legend.y ? 
                   margin.t + plotHeight * (1 - layout.legend.y) : 
                   margin.t;
        }
        
        // Draw legend marker
        if (type === 'scatter' || type === 'line') {
          // Line
          ctx.strokeStyle = color;
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.moveTo(legendX, legendY);
          ctx.lineTo(legendX + 20, legendY);
          ctx.stroke();
          
          // Point
          ctx.fillStyle = color;
          ctx.beginPath();
          ctx.arc(legendX + 10, legendY, 3, 0, 2 * Math.PI);
          ctx.fill();
        } else if (type === 'bar') {
          // Rectangle
          ctx.fillStyle = color;
          ctx.fillRect(legendX, legendY - 5, 20, 10);
          ctx.strokeStyle = 'rgba(0,0,0,0.3)';
          ctx.strokeRect(legendX, legendY - 5, 20, 10);
        }
        
        // Legend text
        ctx.fillStyle = '#333';
        ctx.font = '12px Arial';
        ctx.textAlign = 'left';
        ctx.fillText(trace.name, legendX + 25, legendY + 4);
        
        // Move to next legend item
        legendY += 20;
      }
    });
  }
  
  // Expose Plotly globally
  window.Plotly = Plotly;
  
  return Plotly;
})();