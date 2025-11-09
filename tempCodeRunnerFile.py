while True:
    # Check if there is any input from the user (from the terminal)
    if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
        # Read the input line and remove any extra spaces/newlines
        cmd = sys.stdin.readline().strip()