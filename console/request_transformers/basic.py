class BasicRequestTransformer():
    def __init__(self):
        pass

    def transform_request(self, request):
        return request

    def transform_reply(self, reply):
        err, *rest = reply.split()
        err = int(err)

        if err == 0:
            return 'Success: {}\n'.format(' '.join(rest))
        else:
            return 'Error {}: {}\n'.format(err, ' '.join(rest))
