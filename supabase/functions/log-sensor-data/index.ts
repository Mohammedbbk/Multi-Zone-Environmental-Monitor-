import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'
import { serve } from 'https://deno.land/std@0.177.0/http/server.ts'

const NTFY_TOPIC_URL = "https://ntfy.sh/h1VF9Rm48A4LmvAb"
const NTFY_ACCESS_TOKEN = "tk_zedxf4l8nuqhcpbwlo5wd661n2pb0"
const SEND_NOTIFICATIONS = true

function parseSensorValue(value: any): number | null {
  if (typeof value === 'number' && isFinite(value)) {
    return value
  }
  return null
}

async function sendNtfyNotification(title: string, message: string, priority: number = 4) {
  if (!SEND_NOTIFICATIONS || !NTFY_TOPIC_URL.includes('/') || !NTFY_ACCESS_TOKEN) {
      console.log("Notifications disabled, topic URL invalid, or access token missing.")
      return
  }
  console.log(`Sending notification: ${title} - ${message}`)
  try {
    const response = await fetch(NTFY_TOPIC_URL, {
      method: 'POST',
      headers: {
        'Title': title,
        'Priority': String(priority),
        'Tags': 'warning,thermometer,droplet',
        'Authorization': `Bearer ${NTFY_ACCESS_TOKEN}`
      },
      body: message
    })
    if (!response.ok) {
      console.error(`ntfy notification failed: ${response.status} ${response.statusText}`)
      const errorBody = await response.text()
      console.error(`ntfy error body: ${errorBody}`)
    } else {
        console.log("ntfy notification sent successfully.")
    }
  } catch (error) {
    console.error('Error sending ntfy notification:', error)
  }
}

serve(async (req: Request) => {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method Not Allowed' }), { status: 405, headers: { 'Content-Type': 'application/json' } })
  }
  if (req.headers.get("content-type") !== "application/json") {
      return new Response(JSON.stringify({ error: 'Request must be JSON' }), { status: 415, headers: { 'Content-Type': 'application/json' } })
  }

  try {
    const data = await req.json()

    const z1_data = data.zone1 || {}
    const z2_data = data.zone2 || {}

    const logEntry = {
      z1_temp: parseSensorValue(z1_data.tempC),
      z1_lux: String(z1_data.lux ?? ''),
      z1_alert: Boolean(z1_data.alert ?? false),
      z2_temp: parseSensorValue(z2_data.dhtTempC),
      z2_humidity: parseSensorValue(z2_data.humidity),
      z2_alert: Boolean(z2_data.alert ?? false),
      fan_on: Boolean(data.fan_on ?? false)
    }

    const supabaseUrl = Deno.env.get('MY_SUPABASE_URL')
    const supabaseServiceKey = Deno.env.get('MY_SUPABASE_SERVICE_KEY')
    if (!supabaseUrl || !supabaseServiceKey) {
        console.error('Missing MY_SUPABASE_URL or MY_SUPABASE_SERVICE_KEY env vars')
        return new Response(JSON.stringify({ error: 'Internal configuration error' }), { status: 500, headers: { 'Content-Type': 'application/json' } })
    }
    const supabase = createClient(supabaseUrl, supabaseServiceKey, {
       auth: { persistSession: false, autoRefreshToken: false, detectSessionInUrl: false }
    })

    const { data: insertData, error: dbError } = await supabase
      .from('sensor_logs')
      .insert(logEntry)
      .select()
      .single()

    if (dbError) {
      console.error('Database Insert Error:', dbError)
      console.error('Data causing error:', JSON.stringify(logEntry))
      return new Response(JSON.stringify({ error: 'Database error', details: dbError.message }), { status: 500, headers: { 'Content-Type': 'application/json' } })
    }

    const currentData = insertData || logEntry
    let notificationTitle = ""
    let notificationMessage = ""
    let shouldNotify = false

    if (currentData.z1_alert && currentData.z2_alert) {
        notificationTitle = "ALERT: Zone 1 & Zone 2"
        notificationMessage = `Multiple alerts! Z1(T:${currentData.z1_temp ?? 'N/A'}C L:${currentData.z1_lux}) Z2(T:${currentData.z2_temp ?? 'N/A'}C H:${currentData.z2_humidity ?? 'N/A'}%)`
        shouldNotify = true
    } else if (currentData.z1_alert) {
        notificationTitle = "ALERT: Zone 1"
        notificationMessage = `Alert in Zone 1. Temp: ${currentData.z1_temp ?? 'N/A'}C, Lux: ${currentData.z1_lux}.`
        shouldNotify = true
    } else if (currentData.z2_alert) {
        notificationTitle = "ALERT: Zone 2"
        notificationMessage = `Alert in Zone 2. Temp: ${currentData.z2_temp ?? 'N/A'}C, Humidity: ${currentData.z2_humidity ?? 'N/A'}%.`
        shouldNotify = true
    }

    if (shouldNotify) {
        sendNtfyNotification(notificationTitle, notificationMessage)
    }

    return new Response(JSON.stringify({ status: 'success', data_inserted: currentData }), {
      status: 201,
      headers: { 'Content-Type': 'application/json' }
    })

  } catch (e) {
    console.error('Error processing request:', e)
     let errorMessage = 'Internal Server Error'
     if (e instanceof SyntaxError) { errorMessage = 'Invalid JSON payload' }
     else if (e instanceof Error) { errorMessage = e.message }
    return new Response(JSON.stringify({ error: errorMessage }), {
      status: e instanceof SyntaxError ? 400 : 500,
      headers: { 'Content-Type': 'application/json' }
    })
  }
})
