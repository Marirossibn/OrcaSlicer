// eslint-disable-next-line
import { isObject, extend } from './utils';
import { paramsList } from './params-list';
// @ts-ignore
import Swiper from 'swiper';
export const allowedParams = paramsList.map((key) => key.replace(/_/, ''));
export function getParams(obj = {}) {
    const params = {
        on: {},
    };
    const passedParams = {};
    extend(params, Swiper.defaults);
    extend(params, Swiper.extendedDefaults);
    params._emitClasses = true;
    const rest = {};
    Object.keys(obj).forEach((key) => {
        const _key = key.replace(/^_/, '');
        if (typeof obj[_key] === 'undefined')
            return;
        if (allowedParams.indexOf(_key) >= 0) {
            if (isObject(obj[_key])) {
                params[_key] = {};
                passedParams[_key] = {};
                extend(params[_key], obj[_key]);
                extend(passedParams[_key], obj[_key]);
            }
            else {
                params[_key] = obj[_key];
                passedParams[_key] = obj[_key];
            }
        }
        else {
            rest[_key] = obj[_key];
        }
    });
    return { params, passedParams, rest };
}
//# sourceMappingURL=data:application/json;base64,eyJ2ZXJzaW9uIjozLCJmaWxlIjoiZ2V0LXBhcmFtcy5qcyIsInNvdXJjZVJvb3QiOiIiLCJzb3VyY2VzIjpbIi4uLy4uLy4uLy4uLy4uLy4uL3NyYy9hbmd1bGFyL3NyYy91dGlscy9nZXQtcGFyYW1zLnRzIl0sIm5hbWVzIjpbXSwibWFwcGluZ3MiOiJBQUFBLDJCQUEyQjtBQUMzQixPQUFPLEVBQUUsUUFBUSxFQUFFLE1BQU0sRUFBRSxNQUFNLFNBQVMsQ0FBQztBQUMzQyxPQUFPLEVBQUUsVUFBVSxFQUFFLE1BQU0sZUFBZSxDQUFDO0FBQzNDLGFBQWE7QUFDYixPQUFPLE1BQU0sTUFBTSxRQUFRLENBQUM7QUFFNUIsTUFBTSxDQUFDLE1BQU0sYUFBYSxHQUFHLFVBQVUsQ0FBQyxHQUFHLENBQUMsQ0FBQyxHQUFHLEVBQUUsRUFBRSxDQUFDLEdBQUcsQ0FBQyxPQUFPLENBQUMsR0FBRyxFQUFFLEVBQUUsQ0FBQyxDQUFDLENBQUM7QUFDM0UsTUFBTSxVQUFVLFNBQVMsQ0FBQyxNQUFXLEVBQUU7SUFDckMsTUFBTSxNQUFNLEdBQVE7UUFDbEIsRUFBRSxFQUFFLEVBQUU7S0FDUCxDQUFDO0lBQ0YsTUFBTSxZQUFZLEdBQVEsRUFBRSxDQUFDO0lBQzdCLE1BQU0sQ0FBQyxNQUFNLEVBQUUsTUFBTSxDQUFDLFFBQVEsQ0FBQyxDQUFDO0lBQ2hDLE1BQU0sQ0FBQyxNQUFNLEVBQUUsTUFBTSxDQUFDLGdCQUFnQixDQUFDLENBQUM7SUFDeEMsTUFBTSxDQUFDLFlBQVksR0FBRyxJQUFJLENBQUM7SUFFM0IsTUFBTSxJQUFJLEdBQVEsRUFBRSxDQUFDO0lBQ3JCLE1BQU0sQ0FBQyxJQUFJLENBQUMsR0FBRyxDQUFDLENBQUMsT0FBTyxDQUFDLENBQUMsR0FBVyxFQUFFLEVBQUU7UUFDdkMsTUFBTSxJQUFJLEdBQUcsR0FBRyxDQUFDLE9BQU8sQ0FBQyxJQUFJLEVBQUUsRUFBRSxDQUFDLENBQUM7UUFDbkMsSUFBSSxPQUFPLEdBQUcsQ0FBQyxJQUFJLENBQUMsS0FBSyxXQUFXO1lBQUUsT0FBTztRQUM3QyxJQUFJLGFBQWEsQ0FBQyxPQUFPLENBQUMsSUFBSSxDQUFDLElBQUksQ0FBQyxFQUFFO1lBQ3BDLElBQUksUUFBUSxDQUFDLEdBQUcsQ0FBQyxJQUFJLENBQUMsQ0FBQyxFQUFFO2dCQUN2QixNQUFNLENBQUMsSUFBSSxDQUFDLEdBQUcsRUFBRSxDQUFDO2dCQUNsQixZQUFZLENBQUMsSUFBSSxDQUFDLEdBQUcsRUFBRSxDQUFDO2dCQUN4QixNQUFNLENBQUMsTUFBTSxDQUFDLElBQUksQ0FBQyxFQUFFLEdBQUcsQ0FBQyxJQUFJLENBQUMsQ0FBQyxDQUFDO2dCQUNoQyxNQUFNLENBQUMsWUFBWSxDQUFDLElBQUksQ0FBQyxFQUFFLEdBQUcsQ0FBQyxJQUFJLENBQUMsQ0FBQyxDQUFDO2FBQ3ZDO2lCQUFNO2dCQUNMLE1BQU0sQ0FBQyxJQUFJLENBQUMsR0FBRyxHQUFHLENBQUMsSUFBSSxDQUFDLENBQUM7Z0JBQ3pCLFlBQVksQ0FBQyxJQUFJLENBQUMsR0FBRyxHQUFHLENBQUMsSUFBSSxDQUFDLENBQUM7YUFDaEM7U0FDRjthQUFNO1lBQ0wsSUFBSSxDQUFDLElBQUksQ0FBQyxHQUFHLEdBQUcsQ0FBQyxJQUFJLENBQUMsQ0FBQztTQUN4QjtJQUNILENBQUMsQ0FBQyxDQUFDO0lBRUgsT0FBTyxFQUFFLE1BQU0sRUFBRSxZQUFZLEVBQUUsSUFBSSxFQUFFLENBQUM7QUFDeEMsQ0FBQyIsInNvdXJjZXNDb250ZW50IjpbIi8vIGVzbGludC1kaXNhYmxlLW5leHQtbGluZVxuaW1wb3J0IHsgaXNPYmplY3QsIGV4dGVuZCB9IGZyb20gJy4vdXRpbHMnO1xuaW1wb3J0IHsgcGFyYW1zTGlzdCB9IGZyb20gJy4vcGFyYW1zLWxpc3QnO1xuLy8gQHRzLWlnbm9yZVxuaW1wb3J0IFN3aXBlciBmcm9tICdzd2lwZXInO1xuXG5leHBvcnQgY29uc3QgYWxsb3dlZFBhcmFtcyA9IHBhcmFtc0xpc3QubWFwKChrZXkpID0+IGtleS5yZXBsYWNlKC9fLywgJycpKTtcbmV4cG9ydCBmdW5jdGlvbiBnZXRQYXJhbXMob2JqOiBhbnkgPSB7fSkge1xuICBjb25zdCBwYXJhbXM6IGFueSA9IHtcbiAgICBvbjoge30sXG4gIH07XG4gIGNvbnN0IHBhc3NlZFBhcmFtczogYW55ID0ge307XG4gIGV4dGVuZChwYXJhbXMsIFN3aXBlci5kZWZhdWx0cyk7XG4gIGV4dGVuZChwYXJhbXMsIFN3aXBlci5leHRlbmRlZERlZmF1bHRzKTtcbiAgcGFyYW1zLl9lbWl0Q2xhc3NlcyA9IHRydWU7XG5cbiAgY29uc3QgcmVzdDogYW55ID0ge307XG4gIE9iamVjdC5rZXlzKG9iaikuZm9yRWFjaCgoa2V5OiBzdHJpbmcpID0+IHtcbiAgICBjb25zdCBfa2V5ID0ga2V5LnJlcGxhY2UoL15fLywgJycpO1xuICAgIGlmICh0eXBlb2Ygb2JqW19rZXldID09PSAndW5kZWZpbmVkJykgcmV0dXJuO1xuICAgIGlmIChhbGxvd2VkUGFyYW1zLmluZGV4T2YoX2tleSkgPj0gMCkge1xuICAgICAgaWYgKGlzT2JqZWN0KG9ialtfa2V5XSkpIHtcbiAgICAgICAgcGFyYW1zW19rZXldID0ge307XG4gICAgICAgIHBhc3NlZFBhcmFtc1tfa2V5XSA9IHt9O1xuICAgICAgICBleHRlbmQocGFyYW1zW19rZXldLCBvYmpbX2tleV0pO1xuICAgICAgICBleHRlbmQocGFzc2VkUGFyYW1zW19rZXldLCBvYmpbX2tleV0pO1xuICAgICAgfSBlbHNlIHtcbiAgICAgICAgcGFyYW1zW19rZXldID0gb2JqW19rZXldO1xuICAgICAgICBwYXNzZWRQYXJhbXNbX2tleV0gPSBvYmpbX2tleV07XG4gICAgICB9XG4gICAgfSBlbHNlIHtcbiAgICAgIHJlc3RbX2tleV0gPSBvYmpbX2tleV07XG4gICAgfVxuICB9KTtcblxuICByZXR1cm4geyBwYXJhbXMsIHBhc3NlZFBhcmFtcywgcmVzdCB9O1xufVxuIl19