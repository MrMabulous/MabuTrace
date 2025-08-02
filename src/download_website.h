/*
 * Copyright (C) 2020 Matthias Bühlmann
 *
 * This file is part of MabuTrace.
 *
 * MabuTrace is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MabuTrace is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MabuTrace.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __DOWNLOAD_WEBSITE__
#define __DOWNLOAD_WEBSITE__

static const char* download_html = R"html_string(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Trace Capture</title>
    <style>
        body {
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: #f0f0f0;
            text-align: center;
        }

        .button-container {
            margin-top: 20px;
            display: flex;
            gap: 10px;
        }

        button {
            padding: 12px 24px;
            font-size: 16px;
            cursor: pointer;
            border: 1px solid #ccc;
            border-radius: 4px;
            background-color: #fff;
            transition: background-color 0.2s;
        }

        button:hover:not(:disabled) {
            background-color: #f8f8f8;
        }
        
        button:disabled {
            cursor: not-allowed;
            background-color: #e9e9e9;
            color: #aaa;
            border-color: #ddd;
        }
        
        #status-indicator {
            width: 38px;
            height: 38px;
            display: flex;
            justify-content: center;
            align-items: center;
            margin-top: 20px;
        }

        /* The CSS Spinner */
        .spinner {
            border: 4px solid #f3f3f3; /* Light grey base */
            border-top: 4px solid #007bff; /* Blue spinning part */
            border-radius: 50%;
            width: 30px;
            height: 30px;
            animation: spin 1s linear infinite;
        }

        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }

        /* Checkmark styling */
        .checkmark {
            font-size: 30px;
            color: #28a745;
        }

        #info {
            font-size: 14px;
            color: #666;
            margin-top: 10px;
            height: 20px; /* Reserve space to prevent layout shift */
        }
    </style>
</head>
<body>

    <button id="captureButton">Capture Trace</button>
    
    <div id="status-indicator"></div>

    <p id="info"></p>

    <div class="button-container">
        <button id="saveTraceButton" disabled>Save Trace</button>
        <button id="openPerfettoButton" disabled>Open Trace in Perfetto</button>
    </div>

    <script>
        const captureButton = document.getElementById('captureButton');
        const saveTraceButton = document.getElementById('saveTraceButton');
        const openPerfettoButton = document.getElementById('openPerfettoButton');
        const statusIndicator = document.getElementById('status-indicator');
        const info = document.getElementById('info');

        let traceBlob = null; // Variable to hold the downloaded trace blob

        // Resets the UI to the initial state, typically after an error.
        const resetUIToInitial = () => {
            captureButton.disabled = false;
            saveTraceButton.disabled = true;
            openPerfettoButton.disabled = true;
            statusIndicator.innerHTML = '';
            info.textContent = '';
            traceBlob = null;
        };

        const setSpinner = () => {
            statusIndicator.innerHTML = '<div class="spinner"></div>';
        };

        const setCheckmark = () => {
            statusIndicator.innerHTML = '<div class="checkmark">✓</div>';
        };

        captureButton.addEventListener('click', async () => {
            // 1. Prepare UI for loading state. Disable all buttons during download.
            captureButton.disabled = true;
            saveTraceButton.disabled = true;
            openPerfettoButton.disabled = true;
            setSpinner(); // Show spinner (replaces checkmark if present)
            info.textContent = 'Downloading trace...';

            const url = `${window.location.origin}/trace.json`;

            try {
                const response = await fetch(url);

                if (!response.ok) {
                    throw new Error(`HTTP error! Status: ${response.status}`);
                }
                
                // Read the response stream chunk by chunk
                const reader = response.body.getReader();
                const chunks = [];
                while (true) {
                    const { done, value } = await reader.read();
                    if (done) {
                        break;
                    }
                    chunks.push(value);
                }
                
                // Assemble the chunks into a Blob
                traceBlob = new Blob(chunks, { type: 'application/json' });

                // 2. Update UI on success. Enable all buttons.
                setCheckmark();
                info.textContent = 'Download complete. Ready to save or open.';
                saveTraceButton.disabled = false;
                openPerfettoButton.disabled = false;
                captureButton.disabled = false; // Re-enable capture for a new run

            } catch (error) {
                console.error('Failed to capture trace:', error);
                info.textContent = `Error: ${error.message}`;
                alert('Failed to capture trace. Check the console for details.');
                resetUIToInitial(); // On error, reset completely
            }
        });

        saveTraceButton.addEventListener('click', async () => {
            if (!traceBlob) {
                alert('No trace data available. Please capture a trace first.');
                return;
            }
            info.textContent = 'Opening save dialog...';
            
            if (window.showSaveFilePicker) {
                try {
                    const handle = await window.showSaveFilePicker({
                        suggestedName: 'trace.json',
                        types: [{ description: 'JSON Trace File', accept: { 'application/json': ['.json'] } }],
                    });
                    const writable = await handle.createWritable();
                    await writable.write(traceBlob);
                    await writable.close();
                    info.textContent = 'Trace saved successfully. Open it now in chrome://tracing';
                    return;
                } catch (err) {
                    if (err.name === 'AbortError') {
                        info.textContent = 'Save cancelled.';
                        return;
                    }
                }
            }
            
            try {
                info.textContent = 'Using fallback save method...';
                const link = document.createElement('a');
                link.href = URL.createObjectURL(traceBlob);
                link.download = 'trace.json';
                document.body.appendChild(link);
                link.click();
                document.body.removeChild(link);
                URL.revokeObjectURL(link.href);
                info.textContent = 'Trace downloaded. Open it now in chrome://tracing';
            } catch (err) {
                console.error('Fallback save failed:', err);
                info.textContent = 'Save failed.';
            }
        });

        openPerfettoButton.addEventListener('click', async () => {
            if (!traceBlob) {
                alert('No trace data available. Please capture a trace first.');
                return;
            }

            info.textContent = 'Opening Perfetto and waiting for it to be ready...';
            const PERFETTO_ORIGIN = 'https://ui.perfetto.dev';
            const win = window.open(PERFETTO_ORIGIN);

            if (!win) {
                info.textContent = 'Popup blocked! Please allow popups for this site.';
                alert('Popup blocked! Please allow popups for this site.');
                return;
            }

            const traceBuffer = await traceBlob.arrayBuffer();
            let timer;

            const onMessageHandler = (evt) => {
                if (evt.origin !== PERFETTO_ORIGIN || evt.data !== 'PONG') return;
                
                window.clearInterval(timer);
                win.postMessage(traceBuffer, PERFETTO_ORIGIN, [traceBuffer]);
                info.textContent = 'Trace sent to Perfetto!';
                window.removeEventListener('message', onMessageHandler);
            };

            window.addEventListener('message', onMessageHandler);
            timer = setInterval(() => win.postMessage('PING', PERFETTO_ORIGIN), 50);
        });
    </script>

</body>
</html>
)html_string";

#endif