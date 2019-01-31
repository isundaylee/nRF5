class ConfigClientRequestTransformer():
    def __init__(self):
        pass

    def transform_request(self, request):
        _, addr, op, *rest = request.split()

        if op in ('reset'):
            return request

        raise ValueError("Unknown config client request: {}".format(request))

    def transform_reply(self, reply):
        err, *rest = reply.split()
        err = int(err)

        if err == 0:
            return 'Success: {}\n'.format(' '.join(rest))
        else:
            return 'Error {}: {}\n'.format(err, ' '.join(rest))
