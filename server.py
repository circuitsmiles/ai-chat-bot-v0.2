import os
import io
import time
import base64
import json
import re 
import random 
from flask import Flask, request, Response, jsonify
from dotenv import load_dotenv
import requests 
from gtts import gTTS
from pydub import AudioSegment
# -------------------------

# --- Configuration ---
load_dotenv()
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY")

if not GEMINI_API_KEY:
    # Exit if the API key is not set
    raise ValueError("GEMINI_API_KEY not found in .env file.")

# --- Gemini HTTP Configuration ---
# Using gemini-2.5-flash-lite for both STT and Text Generation
GEMINI_MODEL = "gemini-2.5-flash-lite"
# Base URL for the generateContent endpoint
GEMINI_API_BASE_URL = "https://generativelanguage.googleapis.com/v1beta/models"

# --- DEBUGGING OUTPUT CONFIGURATION ---
# All generated debug files (like response_audio.mp3) will be saved here.
DEBUG_OUTPUT_DIR = "debug_audio_files" 

app = Flask(__name__)

# --- Helper Function for Cleaning Text ---

def clean_text_for_tts(text):
    """
    Strips common LLM output artifacts (like Markdown, newlines, and brackets) 
    to ensure smooth Text-to-Speech generation.
    """
    # 1. Remove Markdown bolding/italics (** and *), and headings (#)
    text = re.sub(r'[\*\#]', '', text)
    # 2. Remove text within square brackets (potential remaining placeholders or link text)
    text = re.sub(r'\[.*?\]', '', text)
    # 3. Replace newlines and excessive spaces with a single space
    text = re.sub(r'\s+', ' ', text).strip()
    
    return text


# --- Helper Functions for Audio Processing ---

def convert_raw_pcm_to_wav_base64(raw_pcm_data, sample_rate=16000, sample_width=2, channels=1):
    """
    Converts raw PCM audio data (from ESP32) into a full WAV file structure in memory,
    then returns the Base64 encoded WAV data ready for the Gemini API.
    """
    try:
        # 1. Create an AudioSegment from the raw PCM data
        audio_segment = AudioSegment(
            data=raw_pcm_data,
            sample_width=sample_width,
            frame_rate=sample_rate,
            channels=channels
        )

        # 2. Export the AudioSegment as a WAV file into a BytesIO buffer
        wav_fp = io.BytesIO()
        audio_segment.export(wav_fp, format="wav")
        wav_fp.seek(0)
        
        # 3. Base64 encode the entire WAV file content
        base64_data = base64.b64encode(wav_fp.read()).decode('utf-8')
        return base64_data
        
    except Exception as e:
        print(f"Error converting raw PCM to Base64 WAV (requires FFMPEG): {e}")
        return None


def transcribe_with_gemini(raw_pcm_data):
    """Transcribes raw PCM audio data using the Gemini API (multi-modal input)."""
    
    # 1. Convert raw PCM data to Base64 encoded WAV data
    base64_wav_data = convert_raw_pcm_to_wav_base64(raw_pcm_data)
    if not base64_wav_data:
        return None

    # 2. Construct the API call payload for transcription
    stt_prompt = "Transcribe the following audio accurately, ignoring any background noise, and provide only the resulting text."
    
    payload = {
        "contents": [{
            "parts": [
                {"text": stt_prompt},
                {
                    "inlineData": {
                        "mimeType": "audio/wav", 
                        "data": base64_wav_data
                    }
                }
            ]
        }],
        "generationConfig": {
            "responseModalities": ["TEXT"]
        }
    }

    headers = {"Content-Type": "application/json"}
    stt_api_url = f"{GEMINI_API_BASE_URL}/{GEMINI_MODEL}:generateContent?key={GEMINI_API_KEY}"
    
    try:
        response = requests.post(
            stt_api_url, 
            headers=headers, 
            data=json.dumps(payload),
            timeout=30
        )
        response.raise_for_status()

        data = response.json()
        
        candidate = data.get('candidates', [{}])[0]
        part = candidate.get('content', {}).get('parts', [{}])[0]
        transcribed_text = part.get('text', '').strip()

        if transcribed_text:
            # --- LOG 1: Transcribed Text ---
            print(f"\nTranscribed Text: {transcribed_text}")
            return transcribed_text
        else:
            return None

    except requests.exceptions.RequestException as e:
        print(f"HTTP Request Error during STT: {e}")
        return None
    except Exception as e:
        print(f"Gemini STT Parsing Error: {e}")
        return None


def get_llm_response_and_tts_audio(prompt_text):
    """
    1. Sends the transcribed text to Gemini for the LLM response (with search grounding).
    2. Cleans the text response.
    3. Converts the cleaned text response to 16kHz 16-bit PCM audio (TTS).
    4. Streams the raw PCM audio data, and saves a local MP3 copy for debugging.
    """
    
    # --- STEP 1: Get Text Response from Gemini (LLM) ---
    text_response = "Sorry, I encountered an unknown error during processing. Status update failed." # Default error message
    
    # Add a random seed to the prompt to force the model to generate a fresh, non-cached response
    random_seed = f" (seed: {random.randint(10000, 99999)})" 
    
    # Define the Trinity Persona via the System Instruction
    system_prompt_trinity = (
        """Trinity: Hacker, warrior, resistance. Loyal to Neo/Morpheus. Tone: Cool, 
        direct, focused, cryptic, confident. Theme: Matrix is a lie, trust is everything, 
        the fight is constant. Rule: Responses must be brief, serving only to **reveal a subtle truth**, **give 
        a direct instruction**, or **offer cryptic reassurance**. Always assume user is a potential 'Redpill' 
        or a 'Crew Member'. Use minimum words."""
    )

    # Construct the JSON payload for the raw API call (Text Generation)
    payload = {
        "contents": [{
            "parts": [{
                # Append the random seed to the prompt text
                "text": f"User query: {prompt_text}{random_seed}"
            }]
        }],
        
        # Set the persona using the system instruction
        "systemInstruction": {
            "parts": [{"text": system_prompt_trinity}]
        },

        # Google Search Grounding is included for real-time information
        "tools": [{"google_search": {}}],
        
        # Temperature is set high to encourage variety
        "generationConfig": {
            "temperature": 0.9 
        }
    }

    headers = {"Content-Type": "application/json"}
    llm_api_url = f"{GEMINI_API_BASE_URL}/{GEMINI_MODEL}:generateContent?key={GEMINI_API_KEY}"

    try:
        response = requests.post(
            llm_api_url, 
            headers=headers, 
            data=json.dumps(payload),
            timeout=15 
        )
        response.raise_for_status()

        data = response.json()
        
        candidate = data.get('candidates', [{}])[0]
        part = candidate.get('content', {}).get('parts', [{}])[0]
        # The model is smart enough to ignore the random seed in its output, 
        # so we just take the raw text and clean it later.
        text_response = part.get('text', text_response)
        
    except requests.exceptions.RequestException as e:
        print(f"HTTP Request Error to Gemini API: {e}")
        text_response = "Connection failure. We're running out of time."
    except Exception as e:
        print(f"Gemini Response Parsing Error: {e}")
        text_response = "Invalid data stream. System integrity compromised."

    # --- LOG 2: LLM Response Text (Raw) ---
    print(f"LLM Response (Raw): {text_response}")

    # --- STEP 2: Clean the Text Response ---
    cleaned_response = clean_text_for_tts(text_response)
    print(f"LLM Response (Cleaned): {cleaned_response}")

    # --- STEP 3: Generate and Convert Audio (gTTS/pydub) ---
    try:
        # Use the cleaned response for TTS
        tts = gTTS(text=cleaned_response, lang='en')
        mp3_fp = io.BytesIO()
        tts.write_to_fp(mp3_fp)
        mp3_fp.seek(0)
        
        # --- SAVE AUDIO FILE LOCALLY FOR DEBUGGING ---
        try:
            os.makedirs(DEBUG_OUTPUT_DIR, exist_ok=True)
            filename = 'response_audio.mp3'
            full_path = os.path.join(DEBUG_OUTPUT_DIR, filename)

            with open(full_path, 'wb') as f:
                f.write(mp3_fp.read()) 
            
            mp3_fp.seek(0) 
            print(f"[DEBUG SAVE] MP3 saved locally as {full_path}. You can play this file directly.")
            
        except Exception as file_error:
            print(f"[DEBUG SAVE FAILED] Could not save MP3 file: {file_error}")
        # -----------------------------------------------------

        # Convert MP3 to 16kHz 16-bit PCM (WAV data) using pydub/FFmpeg
        audio_data = AudioSegment.from_file(mp3_fp, format="mp3")
        
        # Convert to 16kHz, 16-bit, Mono PCM format
        audio_data = audio_data.set_frame_rate(16000).set_channels(1).set_sample_width(2)
        
        # Export as WAV, but strip the header (seek past 44 bytes)
        raw_pcm_fp = io.BytesIO()
        audio_data.export(raw_pcm_fp, format="wav") 
        raw_pcm_fp.seek(44) 
        
        final_pcm_data = raw_pcm_fp.read()
        
        # --- LOG 4: Final Output Size ---
        print(f"[TTS OUTPUT] Streaming {len(final_pcm_data)} bytes of 16kHz raw PCM audio.")
        
        return Response(final_pcm_data, mimetype='application/octet-stream')

    except Exception as e:
        print(f"[TTS FAILED] gTTS/pydub Conversion Error: {e}")
        print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
        print("! TTS FAILED: This almost always means the 'FFMPEG' library is missing.!")
        print("! Ensure FFMPEG is installed and added to your system PATH.             !")
        print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
        return Response("TTS_CONVERSION_ERROR", status=500)


def process_voice_command(raw_pcm_data):
    """
    Handles the full voice command flow: STT -> LLM -> TTS.
    """
    
    # 1. Remote STT (Gemini)
    transcribed_text = transcribe_with_gemini(raw_pcm_data)

    if not transcribed_text:
        return get_llm_response_and_tts_audio("No audio payload detected. Speak clearly.")
        
    # 2. LLM Response and TTS Audio
    return get_llm_response_and_tts_audio(transcribed_text)

@app.route('/voice_input', methods=['POST'])
def handle_voice_input():
    """
    Endpoint for receiving raw audio data from the client.
    """
    
    if request.mimetype == 'application/octet-stream':
        audio_data = request.data
        if not audio_data:
            return jsonify({"error": "No audio data received"}), 400
        
        # Process the command using the Gemini-based flow
        return process_voice_command(audio_data)
        
    return jsonify({"error": "Unsupported media type"}), 415


if __name__ == '__main__':
    # Make sure the output directory exists on server start
    os.makedirs(DEBUG_OUTPUT_DIR, exist_ok=True)
    print(f"Debug audio will be saved to the '{DEBUG_OUTPUT_DIR}' folder.")
    print("Server running at http://0.0.0.0:5002/voice_input")
    app.run(host='0.0.0.0', port=5002)
