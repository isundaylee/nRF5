class ConfigClientRequestTransformer():
    def __init__(self):
        pass

    def transform_request(self, request):
        _, addr, op, *rest = request.split()

        if op in ('reset'):
            return request
        elif op == 'pub_set':
            return self._transform_pub_set_request(request)

        raise ValueError("Unknown config client request: {}".format(request))

    def transform_reply(self, reply):
        err, *rest = reply.split()
        err = int(err)

        if err == 0:
            return 'Success: {}\n'.format(' '.join(rest))
        else:
            return 'Error {}: {}\n'.format(err, ' '.join(rest))

    def _transform_pub_set_request(self, request):
        # config ADDR pub_set ELEMENT_ID COMPANY_ID:MODEL_ID PUB_ADDR TTL PERIOD_100MS
        _, addr, _, element_id, model_id, pub_addr, ttl, period_s = \
            request.split()

        company_id, model_id = model_id.split(':')

        return "config {} pub_set {} {} {} {} {} {}".format(
            addr,
            element_id,
            company_id,
            model_id,
            pub_addr,
            ttl,
            int(10 * float(period_s)))
