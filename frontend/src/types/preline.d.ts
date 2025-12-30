declare module 'preline/preline' {
  export interface IStaticMethods {
    autoInit(collection?: string | string[]): void;
  }

  export const HSStaticMethods: IStaticMethods;
}
