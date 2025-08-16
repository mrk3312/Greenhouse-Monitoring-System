from flask import Flask, render_template, request, jsonify, session
import influxdb_client
from influxdb_client import InfluxDBClient
from influxdb_client.client.write_api import SYNCHRONOUS

app = Flask(__name__)
app.secret_key = "hello"
INFLUXDB_URL = "http://127.0.0.1:8086"
INFLUXDB_TOKEN = "TBwrAcuHwApM30Ffk4RJe_x_gzy5FdtNB-NxvbZMCpZxXfNWUkBsm9IFR_4VvZgzlAApAXtIn3Rx-E9JuCcW3g=="
INFLUXDB_ORG = "GreenhouseManagementSystem"
INFLUXDB_BUCKET = "SensorData"
client = InfluxDBClient(url=INFLUXDB_URL, token=INFLUXDB_TOKEN, org= INFLUXDB_ORG, bucket=INFLUXDB_BUCKET, timeout=30000)
query_api = client.query_api()
write_api = client.write_api(write_options=SYNCHRONOUS)

writeData = None
@app.route('/write-data', methods = ['POST'])
def WriteData():
    global writeData
    if request.is_json:
        writeData = request.get_json()
        if "warning" in writeData:
            return jsonify({"warning": "received successfully"}), 200
        else:
            try:
                point = influxdb_client.Point("Data").tag("location", "kitchen").field("temperature", float(writeData["Temperature"])).field("humidity", float(writeData["Humidity"])).field("light_intensity", float(writeData["Light"]))
            except:
                print("Cannot configure point")
            try:
                write_api.write(bucket=INFLUXDB_BUCKET, org=INFLUXDB_ORG, record=point)
                print("Data written successfully")
            except:
                print("Cannot write the data in the database")
            return jsonify({"message": "Data received successfully"}), 200
    else:

        return jsonify({"message": "Invalid JSON"}), 400

@app.route('/show-data', methods =['GET', 'POST'])
def ShowData():
    if 'filters' not in session:
        session['filters'] = [["temperature", True], ["humidity", True], ["light", True], ["3"], ["d"]]

    if request.method == "POST":
        session['filters'][0][1] = "temperature" in request.form
        session['filters'][1][1] = "humidity" in request.form
        session['filters'][2][1] = "light" in request.form
        if request.form.get("range_count").isdigit() and int(request.form.get("range_count")) != 0:
            session['filters'][3][0] = request.form.get("range_count")

        else:
            session['filters'][3][0] = "3"
        session['filters'][4][0] = request.form.get("range_type")

    print(session.get('filters'))
    filters = session['filters']
    query = f'''

        import "math"
    
        from(bucket: "{INFLUXDB_BUCKET}")
        |> range (start: -{filters[3][0]}{filters[4][0]})
        |> aggregateWindow(every: 30s, fn: mean)
        |> filter(fn: (r) => r._measurement == "Data" and exists r._value)
        |> map(fn: (r) => ({{r with _value: (math.round(x: r._value * 1000.0) / 1000.0)}}))
        |> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
        |> timeShift(duration: 2h)
    ''' 
    try: 
        result = query_api.query(org=INFLUXDB_ORG, query=query)
        readData = []
        for table in result:
            for record in table.records:
                time = record.get_time()
                time = str(time)
                time = list(time)
                time = time[5:]
                numberDoubleDots = 0
                for i in range(len(time)):
                    if numberDoubleDots == 2:
                        time = time[:i-1]
                        time = "".join(time)
                        break
                    if time[i] == ":":
                        numberDoubleDots+=1
                entry = {
                    "time": time,
                    "temperature": record.values.get("temperature"),
                    "humidity": record.values.get("humidity"),
                    "light_intensity": record.values.get("light_intensity") / 100
                }
                readData.append(entry)

        if not readData:
            return render_template('index.html', filters=filters)
        try:
            warningStr=writeData.get("warning")
            return render_template('index.html', data=readData, warning=warningStr)
        except:
            return render_template('index.html', data=readData, warning="All conditions normal", filters=filters)

    except Exception as e:
        return f"<h3>error: {str(e)}</h3>"
            
if __name__ == '__main__':
    app.run(host="0.0.0.0", debug=True)