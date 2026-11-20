-- DESCRIPTION: Verilator: VHDL two-state inout port diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- VHDL permits a `bit` (or `bit_vector`) inout port, but V3Tristate requires
-- a four-state net. The frontend must refuse it at the port, with a VHDL
-- diagnostic, rather than let the middle end reject the net type.

entity t is
  port (
    q : inout bit
  );
end entity;

architecture rtl of t is
begin
end architecture;
