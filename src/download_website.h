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

        #captureButton {
            padding: 12px 24px;
            font-size: 16px;
            cursor: pointer;
            border: 1px solid #ccc;
            border-radius: 4px;
            background-color: #fff;
            margin-bottom: 20px;
        }

        #captureButton:disabled {
            cursor: not-allowed;
            background-color: #e9e9e9;
        }
        
        /* The CSS Spinner */
        #spinner {
            border: 4px solid #f3f3f3; /* Light grey base */
            border-top: 4px solid #007bff; /* Blue spinning part */
            border-radius: 50%;
            width: 30px;
            height: 30px;
            animation: spin 1s linear infinite;
        }

        /* The animation for rotation */
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
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
    
    <!-- The spinner element will be shown/hidden by the script -->
    <div id="spinner" style="display: none;"></div>

    <p id="info"></p>

    <script>
        const captureButton = document.getElementById('captureButton');
        const spinner = document.getElementById('spinner');
        const info = document.getElementById('info');

        captureButton.addEventListener('click', async () => {
            // 1. Prepare UI for loading state
            captureButton.disabled = true;
            spinner.style.display = 'block';
            info.textContent = 'Downloading trace...';

            const url = `${window.location.origin}/trace.json`;

            try {
                const response = await fetch(url);

                if (!response.ok) {
                    throw new Error(`HTTP error! Status: ${response.status}`);
                }
                
                // Read the response stream. Since we are using a spinner,
                // we no longer need to calculate progress.
                const reader = response.body.getReader();
                const chunks = [];

                while (true) {
                    const { done, value } = await reader.read();
                    if (done) {
                        break;
                    }
                    chunks.push(value);
                }
                
                info.textContent = 'Download complete. Opening save dialog...';

                const blob = new Blob(chunks, { type: 'application/json' });

                // 2. SAVE THE FILE: Try modern API first, then fall back.
                if (window.showSaveFilePicker) {
                    try {
                        const handle = await window.showSaveFilePicker({
                            suggestedName: 'trace.json',
                            types: [{
                                description: 'JSON Trace File',
                                accept: { 'application/json': ['.json'] },
                            }],
                        });
                        const writable = await handle.createWritable();
                        await writable.write(blob);
                        await writable.close();
                        info.textContent = 'Trace saved successfully!';
                        return; // Success, exit function
                    } catch (err) {
                        if (err.name === 'AbortError') {
                            info.textContent = 'Save cancelled.';
                            return; 
                        }
                        // Fall through to legacy method on other errors
                    }
                }

                // Fallback for older browsers or non-secure contexts
                info.textContent = 'Using fallback save method...';
                const blobUrl = URL.createObjectURL(blob);
                const link = document.createElement('a');
                link.href = blobUrl;
                link.download = 'trace.json';
                
                document.body.appendChild(link);
                link.click();
                document.body.removeChild(link);

                URL.revokeObjectURL(blobUrl);
                info.textContent = 'Trace download initiated!';

            } catch (error) {
                console.error('Failed to capture trace:', error);
                info.textContent = `Error: ${error.message}`;
                alert('Failed to capture trace. Check the console for details.');
            } finally {
                // 3. Reset the UI
                captureButton.disabled = false;
                spinner.style.display = 'none';
                
                // Keep the final status message visible for a few seconds
                setTimeout(() => { 
                    if (info.textContent !== 'Save cancelled.') {
                        info.textContent = ''; 
                    }
                }, 4000);
            }
        });
    </script>

</body>
</html>
)html_string";

#endif