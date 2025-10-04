import os
import io
import time
from flask import Flask, request, Response, jsonify
from dotenv import load_dotenv
import requests 
from gtts import gTTS
from pydub import AudioSegment
# --- Local STT Imports ---
from vosk import Model, KaldiRecognizer
import json
# -------------------------

# --- Configuration ---
load_dotenv()
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY")

if not GEMINI_API_KEY:
    # Exit if the API key is not set
    raise ValueError("GEMINI_API_KEY not found in .env file.")

# --- Vosk STT Configuration ---
# IMPORTANT: Model folder must be named 'model' and contain the Vosk files.
VOSK_MODEL_PATH = "model" 
# --- Gemini HTTP Configuration ---
GEMINI_MODEL_TEXT = "gemini-2.5-flash-lite"
# Construct the complete API URL using the API key
GEMINI_API_URL = f"https://generativelanguage.googleapis.com/v1beta/models/{GEMINI_MODEL_TEXT}:generateContent?key={GEMINI_API_KEY}"

app = Flask(__name__)

# Initialize the Vosk model
try:
    # Load the Vosk model once at startup
    vosk_model = Model(VOSK_MODEL_PATH)
    print(f"Successfully loaded Vosk model from: {VOSK_MODEL_PATH}")
except Exception as e:
    print(f"Vosk Model Error: {e}. STT will fail. Did you download a model and place it in the '{VOSK_MODEL_PATH}' folder?")
    vosk_model = None

# --- Helper function for Vosk Transcription ---
def transcribe_with_vosk(audio_data):
    """Transcribes raw 16-bit PCM audio data using the local Vosk model."""
    if vosk_model is None:
        return "" # Cannot transcribe if model failed to load

    # Initialize recognizer with the correct sample rate (16000 Hz)
    recognizer = KaldiRecognizer(vosk_model, 16000)

    # Process audio chunk (Vosk handles raw 16-bit PCM data directly)
    recognizer.AcceptWaveform(audio_data)
    
    # Get the final result
    result_json = recognizer.FinalResult()
    try:
        # Vosk returns a JSON string containing the transcribed text
        transcribed_text = json.loads(result_json).get('text', '')
        return transcribed_text
    except Exception as e:
        print(f"Error parsing Vosk result: {e}")
        return ""


def get_audio_response(prompt_text):
    """
    1. Sends the transcribed text to Gemini using a raw HTTP request (LLM).
    2. Gets the text response.
    3. Converts the text response to audio using gTTS (free/local).
    4. Converts gTTS MP3 output to 16kHz 16-bit PCM for ESP32 using pydub.
    5. Streams the raw PCM audio data.
    """
    
    # --- STEP 1: Get Text Response from Gemini using raw requests ---
    print(f"LLM Prompt: {prompt_text}")
    text_response = "Sorry, I encountered an unknown error during processing." # Default error message

    # 1a. Construct the JSON payload for the raw API call
    payload = {
        "contents": [{
            "parts": [{
                "text": f"You are a helpful voice assistant. Respond concisely to the user's query: {prompt_text}"
            }]
        }]
    }

    headers = {
        "Content-Type": "application/json"
    }

    try:
        # 1b. Make the synchronous HTTP POST request
        response = requests.post(
            GEMINI_API_URL, 
            headers=headers, 
            data=json.dumps(payload),
            timeout=15 # Set a reasonable timeout
        )
        response.raise_for_status() # Raise an exception for bad status codes (4xx or 5xx)

        # 1c. Parse the JSON response
        data = response.json()
        
        # Manually navigate the nested structure to extract the text
        candidate = data.get('candidates', [{}])[0]
        part = candidate.get('content', {}).get('parts', [{}])[0]
        text_response = part.get('text', text_response)
        
    except requests.exceptions.RequestException as e:
        print(f"HTTP Request Error to Gemini API: {e}")
        text_response = "Sorry, I failed to connect to the LLM server."
    except Exception as e:
        print(f"Gemini Response Parsing Error: {e}")
        text_response = "Sorry, the LLM returned an invalid response."

    print(f"LLM Response: {text_response}")

    # --- STEP 2 & 3: Generate and Convert Audio (gTTS/pydub - Unchanged) ---
    try:
        # Using gTTS for text-to-speech
        tts = gTTS(text=text_response, lang='en')
        mp3_fp = io.BytesIO()
        tts.write_to_fp(mp3_fp)
        mp3_fp.seek(0)
        
        # Convert MP3 to 16kHz 16-bit PCM (WAV data) using pydub/FFmpeg
        audio_data = AudioSegment.from_file(mp3_fp, format="mp3")
        
        # Convert to 16kHz, 16-bit, Mono PCM format, matching ESP32 I2S config
        audio_data = audio_data.set_frame_rate(16000).set_channels(1).set_sample_width(2)
        
        # Export as WAV, but we strip the header
        raw_pcm_fp = io.BytesIO()
        audio_data.export(raw_pcm_fp, format="wav") 
        # Skip the 44-byte WAV header, sending only raw PCM data for I2S playback
        raw_pcm_fp.seek(44) 
        
        # Return the raw PCM audio bytes to the ESP32
        return Response(raw_pcm_fp.read(), mimetype='application/octet-stream')

    except Exception as e:
        print(f"gTTS/pydub Conversion Error: {e}")
        print("NOTE: If this fails, ensure FFMPEG is installed and running, and check network if gTTS is used.")
        # Fallback response for TTS failure
        return Response("TTS_CONVERSION_ERROR", status=500)


def transcribe_and_respond(audio_data):
    """
    1. Performs local STT using Vosk.
    2. Sends transcribed text to LLM (via requests) for response generation.
    3. Calls get_audio_response to get the final audio stream.
    """
    
    # --- STEP 1: Local STT (Vosk) ---
    print("Performing local STT using Vosk...")
    transcribed_text = transcribe_with_vosk(audio_data)

    if not transcribed_text:
        error_msg = "Transcription failed or returned empty. Check Vosk model status."
        print(error_msg)
        # Pass a failure message to the LLM/gTTS chain so the user hears an error.
        return get_audio_response("I couldn't understand that. Please try speaking again.")
        

    print(f"Vosk Transcription Result: {transcribed_text}")
    
    # --- STEP 2 & 3: LLM Response (via requests) and gTTS Audio ---
    return get_audio_response(transcribed_text)

@app.route('/voice_input', methods=['POST'])
def handle_voice_input():
    """
    Endpoint for receiving raw audio data from the ESP32.
    """
    if request.mimetype == 'application/octet-stream':
        audio_data = request.data
        if not audio_data:
            return jsonify({"error": "No audio data received"}), 400
        
        return transcribe_and_respond(audio_data)
        
    return jsonify({"error": "Unsupported media type"}), 415


if __name__ == '__main__':
    print("Server running at http://0.0.0.0:5002/voice_input")
    app.run(host='0.0.0.0', port=5002)
