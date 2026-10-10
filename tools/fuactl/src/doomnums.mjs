// [rc4l] The action specials and editor numbers template maps use, named as the engine names them and checked against its own definitions by test/doomnums.test.mjs.
export const SPECIAL = Object.freeze({
  Light_ChangeToValue: 112,
  Sector_SetPlaneReflection: 159,
  Sector_Set3DFloor: 160,
  Line_Mirror: 182,
  Sector_SetColor: 212,
});

// [rc4l] PointLight comes from GL's light actors rather than the DECORATE tree, so it is the one number the test cannot check.
export const THING = Object.freeze({
  TechPillar: 48,
  Medikit: 2012,
  Column: 2028,
  UpperStackLookOnly: 9077,
  LowerStackLookOnly: 9078,
  SkyViewpoint: 9080,
  PointLight: 9800,
});
